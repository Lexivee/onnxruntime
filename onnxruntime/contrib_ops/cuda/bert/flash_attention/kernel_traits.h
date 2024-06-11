/******************************************************************************
 * Copyright (c) 2023, Tri Dao.
 ******************************************************************************/
#pragma once

#include <cute/tensor.hpp>

#include <cutlass/cutlass.h>
#include <cutlass/layout/layout.h>
#include <cutlass/numeric_types.h>

using namespace cute;

namespace onnxruntime {
namespace flash {

template <int kHeadDim_, int kBlockM_, int kBlockN_, int kNWarps_, typename elem_type = cutlass::half_t>
struct Flash_kernel_traits {
#if defined(__CUDA_ARCH__) && __CUDA_ARCH__ >= 800
  using Element = elem_type;
  static constexpr bool Has_cp_async = true;
#else
  using Element = cutlass::half_t;
  static constexpr bool Has_cp_async = false;
#endif

  using ElementAccum = float;
  using index_t = uint32_t;

#if defined(__CUDA_ARCH__) && __CUDA_ARCH__ >= 800
  using MMA_Atom_Arch = std::conditional_t<
      std::is_same_v<elem_type, cutlass::half_t>,
      MMA_Atom<SM80_16x8x16_F32F16F16F32_TN>,
      MMA_Atom<SM80_16x8x16_F32BF16BF16F32_TN>>;
#else
  using MMA_Atom_Arch = MMA_Atom<SM75_16x8x8_F32F16F16F32_TN>;
#endif

#if defined(__CUDA_ARCH__) && __CUDA_ARCH__ >= 750
  using SmemCopyAtom = Copy_Atom<SM75_U32x4_LDSM_N, elem_type>;
  using SmemCopyAtomTransposed = Copy_Atom<SM75_U16x8_LDSM_T, elem_type>;
#else
  using SmemCopyAtom = Copy_Atom<DefaultCopy, elem_type>;
  using SmemCopyAtomTransposed = Copy_Atom<DefaultCopy, elem_type>;
#endif
};

// If Share_Q_K_smem is true, that forces Is_Q_in_regs to be true
template <int kHeadDim_, int kBlockM_, int kBlockN_, int kNWarps_,
          bool Is_Q_in_regs_ = false, bool Share_Q_K_smem_ = false, typename elem_type = cutlass::half_t,
          typename Base = Flash_kernel_traits<kHeadDim_, kBlockM_, kBlockN_, kNWarps_, elem_type>>
struct Flash_fwd_kernel_traits : public Base {
  using Element = typename Base::Element;
  using ElementAccum = typename Base::ElementAccum;
  using index_t = typename Base::index_t;
  static constexpr bool Has_cp_async = Base::Has_cp_async;
  using SmemCopyAtom = typename Base::SmemCopyAtom;
  using SmemCopyAtomTransposed = typename Base::SmemCopyAtomTransposed;

  static constexpr bool Share_Q_K_smem = Share_Q_K_smem_;
  static constexpr bool Is_Q_in_regs = Is_Q_in_regs_ || Share_Q_K_smem;

  // The number of threads.
  static constexpr int kNWarps = kNWarps_;
  static constexpr int kNThreads = kNWarps * 32;

  static constexpr int kBlockM = kBlockM_;
  static constexpr int kBlockN = kBlockN_;
  static constexpr int kHeadDim = kHeadDim_;
  static_assert(kHeadDim % 32 == 0);
  static constexpr int kBlockKSmem = kHeadDim % 64 == 0 ? 64 : 32;
  static constexpr int kBlockKGmem = kHeadDim % 128 == 0 ? 128 : (kHeadDim % 64 == 0 ? 64 : 32);
  static constexpr int kSwizzle = kBlockKSmem == 32 ? 2 : 3;

  using TiledMma = TiledMMA<
      typename Base::MMA_Atom_Arch,
      Layout<Shape<Int<kNWarps>, _1, _1>>,  // 4x1x1 or 8x1x1 thread group
      Tile<Int<16 * kNWarps>, _16, _16>>;

  using SmemLayoutAtomQ = decltype(composition(Swizzle<kSwizzle, 3, 3>{},
                                               // This has to be kBlockKSmem, using kHeadDim gives wrong results for d=128
                                               Layout<Shape<_8, Int<kBlockKSmem>>,
                                                      Stride<Int<kBlockKSmem>, _1>>{}));
  using SmemLayoutQ = decltype(tile_to_shape(
      SmemLayoutAtomQ{},
      Shape<Int<kBlockM>, Int<kHeadDim>>{}));

  using SmemLayoutKV = decltype(tile_to_shape(
      SmemLayoutAtomQ{},
      Shape<Int<kBlockN>, Int<kHeadDim>>{}));

  // This has to be kBlockN and not 8, otherwise we get wrong results for d=128
  using SmemLayoutAtomVtransposedNoSwizzle = Layout<Shape<Int<kBlockKSmem>, Int<kBlockN>>,
                                                    Stride<_1, Int<kBlockKSmem>>>;
  using SmemLayoutAtomVtransposed = decltype(composition(Swizzle<kSwizzle, 3, 3>{}, SmemLayoutAtomVtransposedNoSwizzle{}));
  using SmemLayoutVtransposed = decltype(tile_to_shape(
      SmemLayoutAtomVtransposed{},
      Shape<Int<kHeadDim>, Int<kBlockN>>{}));
  // Maybe the VtransposeNoSwizzle just needs to have the right shape
  // And the strides don't matter?
  using SmemLayoutVtransposedNoSwizzle = decltype(tile_to_shape(
      SmemLayoutAtomVtransposedNoSwizzle{},
      Shape<Int<kHeadDim>, Int<kBlockN>>{}));
  // using SmemLayoutVtransposedNoSwizzle = decltype(SmemLayoutVtransposed{}.layout_fn());

  using SmemLayoutAtomO = decltype(composition(Swizzle<kSwizzle, 3, 3>{},
                                               Layout<Shape<Int<8>, Int<kBlockKSmem>>,
                                                      Stride<Int<kBlockKSmem>, _1>>{}));
  using SmemLayoutO = decltype(tile_to_shape(
      SmemLayoutAtomO{},
      Shape<Int<kBlockM>, Int<kHeadDim>>{}));
  using SmemCopyAtomO = Copy_Atom<DefaultCopy, Element>;
  using SmemCopyAtomOaccum = Copy_Atom<DefaultCopy, ElementAccum>;

  static constexpr int kSmemQCount = cute::size(SmemLayoutQ{});
  static constexpr int kSmemKVCount = cute::size(SmemLayoutKV{}) * 2;
  static constexpr int kSmemQSize = kSmemQCount * sizeof(Element);
  static constexpr int kSmemKVSize = kSmemKVCount * sizeof(Element);
  static constexpr int kSmemSize = Share_Q_K_smem ? std::max(kSmemQSize, kSmemKVSize) : kSmemQSize + kSmemKVSize;

  static constexpr int kGmemElemsPerLoad = sizeof(cute::uint128_t) / sizeof(Element);
  static_assert(kHeadDim % kGmemElemsPerLoad == 0, "kHeadDim must be a multiple of kGmemElemsPerLoad");
  // Using kBlockKSmem here is 6-10% faster than kBlockKGmem for d=128 because of bank conflicts.
  // For example, for d=128, smem is split into 2 "pages", each page takes care of columns
  // 0-63 and 64-127. If we have 16 threads per row for gmem read, when we write to smem,
  // thread 0 - 7 will write to the first page and thread 8 - 15 will write to the second page,
  // to the same banks.
  static constexpr int kGmemThreadsPerRow = kBlockKSmem / kGmemElemsPerLoad;
  static_assert(kNThreads % kGmemThreadsPerRow == 0, "kNThreads must be a multiple of kGmemThreadsPerRow");
  using GmemLayoutAtom = cute::Layout<cute::Shape<cute::Int<kNThreads / kGmemThreadsPerRow>, cute::Int<kGmemThreadsPerRow>>,
                                      cute::Stride<cute::Int<kGmemThreadsPerRow>, _1>>;

  // We use CACHEGLOBAL instead of CACHEALWAYS for both Q and K/V, since we won't be reading
  // from the same address by the same threadblock. This is slightly faster.
  using Gmem_copy_struct = std::conditional_t<
      Has_cp_async,
      SM80_CP_ASYNC_CACHEGLOBAL<cute::uint128_t>,
      DefaultCopy>;
  using GmemTiledCopyQKV = decltype(make_tiled_copy(Copy_Atom<Gmem_copy_struct, elem_type>{},
                                                    GmemLayoutAtom{},
                                                    cute::Layout<cute::Shape<_1, _8>>{}));  // Val layout, 8 vals per read
  using GmemTiledCopyO = decltype(make_tiled_copy(Copy_Atom<DefaultCopy, elem_type>{},
                                                  GmemLayoutAtom{},
                                                  cute::Layout<cute::Shape<_1, _8>>{}));  // Val layout, 8 vals per store
  static constexpr int kGmemThreadsPerRowP = kBlockN / kGmemElemsPerLoad;
  static_assert(kNThreads % kGmemThreadsPerRowP == 0, "kNThreads must be a multiple of kGmemThreadsPerRowP");
  using GmemLayoutAtomP = cute::Layout<cute::Shape<cute::Int<kNThreads / kGmemThreadsPerRowP>, cute::Int<kGmemThreadsPerRowP>>,
                                       cute::Stride<cute::Int<kGmemThreadsPerRowP>, _1>>;

  using GmemTiledCopyP = decltype(make_tiled_copy(Copy_Atom<DefaultCopy, elem_type>{},
                                                  GmemLayoutAtomP{},
                                                  cute::Layout<cute::Shape<_1, _8>>{}));  // Val layout, 8 vals per store

  using GmemLayoutAtomOaccum = std::conditional_t<
      kBlockKSmem == 32,
      cute::Layout<cute::Shape<_16, _8>,  // Thread layout, 8 threads per row
                   cute::Stride<_8, _1>>,
      cute::Layout<cute::Shape<_8, _16>,  // Thread layout, 16 threads per row
                   cute::Stride<_16, _1>>>;
  using GmemTiledCopyOaccum = decltype(make_tiled_copy(Copy_Atom<DefaultCopy, ElementAccum>{},
                                                       GmemLayoutAtomOaccum{},
                                                       Layout<Shape<_1, _4>>{}));  // Val layout, 4 vals per store
  using GmemLayoutAtomRotcossin = GmemLayoutAtom;
  using GmemTiledCopyRotcossin = decltype(make_tiled_copy(Copy_Atom<UniversalCopy<uint64_t>, Element>{},
                                                          GmemLayoutAtomRotcossin{},
                                                          Layout<Shape<_1, _4>>{}));  // Val layout, 4 vals per load
  using GmemTiledCopyRotcossinCont = decltype(make_tiled_copy(Copy_Atom<DefaultCopy, Element>{},
                                                              GmemLayoutAtomRotcossin{},
                                                              Layout<Shape<_1, _8>>{}));  // Val layout, 8 vals per load
};

// Is_V_in_regs is an option to reduce smem usage, but will increase register pressue.
// No_double_buffer is another option to reduce smem usage, but will slow things down.
template <int kHeadDim_, int kBlockM_, int kBlockN_, int kNWarps_,
          int AtomLayoutMSdP_ = 1, int AtomLayoutNdKV = 2, int AtomLayoutMdQ = 2,
          bool Is_V_in_regs_ = false, bool No_double_buffer_ = false, typename elem_type = cutlass::half_t,
          typename Base = Flash_kernel_traits<kHeadDim_, kBlockM_, kBlockN_, kNWarps_, elem_type>>
struct Flash_bwd_kernel_traits : public Base {
  using Element = typename Base::Element;
  using ElementAccum = typename Base::ElementAccum;
  using index_t = typename Base::index_t;
  static constexpr bool Has_cp_async = Base::Has_cp_async;
  using SmemCopyAtom = typename Base::SmemCopyAtom;
  using SmemCopyAtomTransposed = typename Base::SmemCopyAtomTransposed;

  static constexpr bool Is_V_in_regs = Is_V_in_regs_;
  static constexpr bool No_double_buffer = No_double_buffer_;

  // The number of threads.
  static constexpr int kNWarps = kNWarps_;
  static constexpr int kNThreads = kNWarps * 32;

  static constexpr int kBlockM = kBlockM_;
  static constexpr int kBlockN = kBlockN_;
  static constexpr int kHeadDim = kHeadDim_;
  static_assert(kHeadDim % 32 == 0);
  static constexpr int kBlockKSmem = kHeadDim % 64 == 0 ? 64 : 32;
  static constexpr int kBlockKGmem = kHeadDim % 128 == 0 ? 128 : (kHeadDim % 64 == 0 ? 64 : 32);
  static constexpr int kSwizzle = kBlockKSmem == 32 ? 2 : 3;

  static constexpr int AtomLayoutMSdP = AtomLayoutMSdP_;
  static_assert(kNWarps % AtomLayoutMSdP == 0);
  static_assert(kNWarps % AtomLayoutNdKV == 0);
  static_assert(kNWarps % AtomLayoutMdQ == 0);

  using TiledMmaSdP = TiledMMA<
      typename Base::MMA_Atom_Arch,
      cute::Layout<cute::Shape<cute::Int<AtomLayoutMSdP>, cute::Int<kNWarps / AtomLayoutMSdP>, _1>>,
      Tile<Int<16 * AtomLayoutMSdP>, Int<16 * kNWarps / AtomLayoutMSdP>, _16>>;

  using TiledMmadKV = TiledMMA<
      typename Base::MMA_Atom_Arch,
      cute::Layout<cute::Shape<cute::Int<AtomLayoutNdKV>, cute::Int<kNWarps / AtomLayoutNdKV>, _1>>,
      Tile<Int<16 * AtomLayoutNdKV>, Int<16 * kNWarps / AtomLayoutNdKV>, _16>>;

  using TiledMmadQ = TiledMMA<
      typename Base::MMA_Atom_Arch,
      cute::Layout<cute::Shape<cute::Int<AtomLayoutMdQ>, cute::Int<kNWarps / AtomLayoutMdQ>, _1>>,  // 2x4x1 or 4x2x1 thread group
      Tile<Int<16 * AtomLayoutMdQ>, Int<16 * kNWarps / AtomLayoutMdQ>, _16>>;

  using SmemLayoutAtomQdO = decltype(composition(Swizzle<kSwizzle, 3, 3>{},
                                                 cute::Layout<cute::Shape<_8, cute::Int<kBlockKSmem>>,
                                                              cute::Stride<cute::Int<kBlockKSmem>, _1>>{}));
  using SmemLayoutQdO = decltype(tile_to_shape(
      SmemLayoutAtomQdO{},
      cute::make_shape(cute::Int<kBlockM>{}, cute::Int<kHeadDim>{})));

  using SmemLayoutAtomKV = decltype(composition(Swizzle<kSwizzle, 3, 3>{},
                                                cute::Layout<cute::Shape<cute::Int<kBlockM / kNWarps>, cute::Int<kBlockKSmem>>,
                                                             cute::Stride<cute::Int<kBlockKSmem>, _1>>{}));
  using SmemLayoutKV = decltype(tile_to_shape(
      // SmemLayoutAtomQdO{},
      SmemLayoutAtomKV{},
      cute::make_shape(cute::Int<kBlockN>{}, cute::Int<kHeadDim>{})));

  using SmemLayoutAtomKtransposedNoSwizzle = Layout<Shape<Int<kBlockKSmem>, Int<kBlockN>>,
                                                    Stride<_1, Int<kBlockKSmem>>>;
  using SmemLayoutAtomKtransposed = decltype(composition(Swizzle<kSwizzle, 3, 3>{}, SmemLayoutAtomKtransposedNoSwizzle{}));
  using SmemLayoutKtransposed = decltype(tile_to_shape(
      SmemLayoutAtomKtransposed{},
      make_shape(Int<kHeadDim>{}, Int<kBlockN>{})));
  // Maybe the KtransposeNoSwizzle just needs to have the right shape
  // And the strides don't matter?
  using SmemLayoutKtransposedNoSwizzle = decltype(tile_to_shape(
      SmemLayoutAtomKtransposedNoSwizzle{},
      make_shape(Int<kHeadDim>{}, Int<kBlockN>{})));
  // using SmemLayoutKtransposedNoSwizzle = decltype(SmemLayoutKtransposed{}.layout_fn());

  // TODO: generalize to other values of kBlockN
  // TODO: what should be the Swizzle here? 3 is faster than 1, and 1 is faster than 2
  // static constexpr int kPBlockN = kBlockN;
  static_assert(kBlockN >= 64);
  // TD [2023-03-19]: Idk why kPBlockN = 16 and kSwizzlePdS=3 is the fastest.
  static constexpr int kPBlockN = 64;
  static_assert(kPBlockN == 16 || kPBlockN == 32 || kPBlockN == 64);
  // static constexpr int kSwizzlePdS = kPBlockN == 16 ? 1 : (kPBlockN == 32 ? 2 : 3);
  static constexpr int kSwizzlePdS = 3;
  using SmemLayoutAtomPdS = decltype(composition(Swizzle<kSwizzlePdS, 3, 3>{},
                                                 cute::Layout<cute::Shape<cute::Int<kBlockM>, cute::Int<kPBlockN>>,
                                                              cute::Stride<cute::Int<kPBlockN>, _1>>{}));
  using SmemLayoutPdS = decltype(tile_to_shape(
      SmemLayoutAtomPdS{},
      cute::make_shape(cute::Int<kBlockM>{}, cute::Int<kBlockN>{})));
  using SmemLayoutAtomPdStransposedNoSwizzle = Layout<Shape<Int<kPBlockN>, Int<kBlockM>>,
                                                      Stride<_1, Int<kPBlockN>>>;
  using SmemLayoutAtomPdStransposed = decltype(composition(Swizzle<kSwizzlePdS, 3, 3>{}, SmemLayoutAtomPdStransposedNoSwizzle{}));
  using SmemLayoutPdStransposed = decltype(tile_to_shape(
      SmemLayoutAtomPdStransposed{},
      make_shape(Int<kBlockN>{}, Int<kBlockM>{})));
  using SmemLayoutPdStransposedNoSwizzle = decltype(tile_to_shape(
      SmemLayoutAtomPdStransposedNoSwizzle{},
      make_shape(Int<kBlockN>{}, Int<kBlockM>{})));
  // using SmemLayoutPdStransposedNoSwizzle = decltype(SmemLayoutPdStransposed{}.layout_fn());
  using SmemCopyAtomPdS = Copy_Atom<DefaultCopy, elem_type>;

  using SmemLayoutAtomQdOtransposedNoSwizzle = Layout<Shape<Int<kBlockKSmem>, Int<kBlockM>>,
                                                      Stride<_1, Int<kBlockKSmem>>>;
  using SmemLayoutAtomQdOtransposed = decltype(composition(Swizzle<kSwizzle, 3, 3>{}, SmemLayoutAtomQdOtransposedNoSwizzle{}));
  using SmemLayoutQdOtransposed = decltype(tile_to_shape(
      SmemLayoutAtomQdOtransposed{},
      make_shape(Int<kHeadDim>{}, Int<kBlockM>{})));
  using SmemLayoutQdOtransposedNoSwizzle = decltype(tile_to_shape(
      SmemLayoutAtomQdOtransposedNoSwizzle{},
      make_shape(Int<kHeadDim>{}, Int<kBlockM>{})));
  // using SmemLayoutQdOtransposedNoSwizzle = decltype(SmemLayoutQdOtransposed{}.layout_fn());

  using SmemLayoutAtomdKV = decltype(composition(Swizzle<kSwizzle, 3, 3>{},
                                                 Layout<Shape<_8, Int<kBlockKSmem>>,
                                                        Stride<Int<kBlockKSmem>, _1>>{}));
  using SmemLayoutdKV = decltype(tile_to_shape(
      SmemLayoutAtomdKV{},
      make_shape(Int<kBlockN>{}, Int<kHeadDim>{})));
  using SmemCopyAtomdKV = Copy_Atom<DefaultCopy, elem_type>;

  using SmemLayoutAtomdQ = decltype(composition(Swizzle<kSwizzle, 3, 3>{},
                                                Layout<Shape<_8, Int<kBlockKSmem>>,
                                                       Stride<Int<kBlockKSmem>, _1>>{}));
  using SmemLayoutdQ = decltype(tile_to_shape(
      SmemLayoutAtomdQ{},
      make_shape(Int<kBlockM>{}, Int<kHeadDim>{})));
  using SmemCopyAtomdQ = Copy_Atom<DefaultCopy, elem_type>;

  static constexpr int kSmemQdOCount = cute::size(SmemLayoutQdO{}) * (No_double_buffer ? 2 : 3);  // Double buffer for sQ
  static constexpr int kSmemKVCount = cute::size(SmemLayoutKV{}) * 2;
  static constexpr int kSmemdSCount = cute::size(SmemLayoutPdS{});
  static constexpr int kSmemPCount = cute::size(SmemLayoutPdS{});
  static constexpr int kSmemdQCount = cute::size(SmemLayoutdQ{});
  //   static constexpr int kSmemdPsumCount = kBlockM;
  static constexpr int kSmemQdOSize = kSmemQdOCount * sizeof(Element);
  static constexpr int kSmemKVSize = kSmemKVCount * sizeof(Element);
  static constexpr int kSmemdSSize = kSmemdSCount * sizeof(Element);
  static constexpr int kSmemPSize = kSmemPCount * sizeof(Element);
  static constexpr int kSmemdQSize = kSmemdQCount * sizeof(Element);
  //   static constexpr int kSmemdPsumSize = kSmemdPsumCount * sizeof(ElementAccum);
  static constexpr int kSmemSize = kSmemQdOSize + (!Is_V_in_regs
                                                       ? kSmemKVSize + kSmemdSSize + std::max(kSmemPSize, kSmemdQSize)
                                                       : std::max(kSmemKVSize, kSmemKVSize / 2 + kSmemdSSize + std::max(kSmemPSize, kSmemdQSize)));
  static constexpr int kSmemSize1colblock = kSmemQdOSize + (!Is_V_in_regs
                                                                ? kSmemKVSize + kSmemdSSize + kSmemPSize
                                                                : std::max(kSmemKVSize, kSmemKVSize / 2 + kSmemdSSize + kSmemPSize));
  static constexpr int kSmemSize1rowblock = kSmemQdOSize / 3 * 2 + kSmemKVSize / 2 * 3 + kSmemdSSize + kSmemPSize;

  static constexpr int kGmemElemsPerLoad = sizeof(cute::uint128_t) / sizeof(Element);
  static_assert(kHeadDim % kGmemElemsPerLoad == 0, "kHeadDim must be a multiple of kGmemElemsPerLoad");
  // Using kBlockKSmem instead of kHeadDim here to avoid bank conflicts, but doesn't seem
  // to affect speed in practice.
  static constexpr int kGmemThreadsPerRow = kBlockKSmem / kGmemElemsPerLoad;
  static_assert(kNThreads % kGmemThreadsPerRow == 0, "kNThreads must be a multiple of kGmemThreadsPerRow");
  using GmemLayoutAtom = cute::Layout<cute::Shape<cute::Int<kNThreads / kGmemThreadsPerRow>, cute::Int<kGmemThreadsPerRow>>,
                                      cute::Stride<cute::Int<kGmemThreadsPerRow>, _1>>;

  // We use CACHEGLOBAL instead of CACHEALWAYS for both Q and K/V, since we won't be reading
  // from the same address by the same threadblock. This is slightly faster.
  using Gmem_copy_struct = std::conditional_t<
      Has_cp_async,
      SM80_CP_ASYNC_CACHEGLOBAL<cute::uint128_t>,
      DefaultCopy>;
  using GmemTiledCopyQKV = decltype(make_tiled_copy(Copy_Atom<Gmem_copy_struct, elem_type>{},
                                                    GmemLayoutAtom{},
                                                    cute::Layout<cute::Shape<_1, _8>>{}));  // Val layout, 8 vals per read
  using GmemTiledCopydO = decltype(make_tiled_copy(Copy_Atom<DefaultCopy, elem_type>{},
                                                   GmemLayoutAtom{},
                                                   cute::Layout<cute::Shape<_1, _8>>{}));  // Val layout, 8 vals per store
  using GmemTiledCopydKV = decltype(make_tiled_copy(Copy_Atom<DefaultCopy, elem_type>{},
                                                    GmemLayoutAtom{},
                                                    cute::Layout<cute::Shape<_1, _8>>{}));  // Val layout, 8 vals per store
  using GmemTiledCopydQ = decltype(make_tiled_copy(Copy_Atom<DefaultCopy, elem_type>{},
                                                   GmemLayoutAtom{},
                                                   cute::Layout<cute::Shape<_1, _8>>{}));  // Val layout, 8 vals per store
  using GmemLayoutAtomdQaccum = std::conditional_t<
      kBlockKSmem == 32,
      cute::Layout<cute::Shape<_32, _8>,  // Thread layout, 8 threads per row
                   cute::Stride<_8, _1>>,
      cute::Layout<cute::Shape<_16, _16>,  // Thread layout, 16 threads per row
                   cute::Stride<_16, _1>>>;
  using GmemTiledCopydQaccum = decltype(make_tiled_copy(Copy_Atom<DefaultCopy, ElementAccum>{},
                                                        GmemLayoutAtomdQaccum{},
                                                        cute::Layout<cute::Shape<_1, _4>>{}));  // Val layout, 4 vals per store

  using GmemTiledCopydQaccumAtomicAdd = decltype(make_tiled_copy(Copy_Atom<DefaultCopy, ElementAccum>{},
                                                                 cute::Layout<cute::Shape<_8, _32>,  // Thread layout, 8 threads per row
                                                                              cute::Stride<_32, _1>>{},
                                                                 cute::Layout<cute::Shape<_1, _1>>{}));  // Val layout, 1 val per store
};

}  // namespace flash
}  // namespace onnxruntime
