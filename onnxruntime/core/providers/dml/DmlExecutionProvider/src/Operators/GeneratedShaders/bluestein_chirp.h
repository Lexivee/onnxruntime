#if 0
//
// Generated by Microsoft (R) D3D Shader Disassembler
//
//
// Input signature:
//
// Name                 Index   Mask Register SysValue  Format   Used
// -------------------- ----- ------ -------- -------- ------- ------
// no Input
//
// Output signature:
//
// Name                 Index   Mask Register SysValue  Format   Used
// -------------------- ----- ------ -------- -------- ------- ------
// no Output
cs_5_0
dcl_globalFlags refactoringAllowed
dcl_constantbuffer CB0[1], immediateIndexed
dcl_uav_structured u0, 4
dcl_uav_structured u1, 4
dcl_input vThreadID.x
dcl_temps 4
dcl_thread_group 64, 1, 1
iadd r0.x, vThreadID.x, cb0[0].x
ult r0.y, r0.x, cb0[0].y
if_nz r0.y
  ishl r0.y, cb0[0].z, l(1)
  iadd r0.y, r0.y, l(-2)
  mov r0.z, l(1)
  loop 
    ult r0.w, r0.y, r0.z
    breakc_nz r0.w
    ishl r0.z, r0.z, l(1)
  endloop 
  ishl r0.y, r0.x, l(1)
  bfi r0.w, l(31), l(1), r0.x, l(1)
  ieq r1.x, cb0[0].w, l(1)
  ult r1.y, r0.x, cb0[0].z
  if_nz r1.y
    movc r1.y, r1.x, l(3.141593), l(-3.141593)
    utof r1.z, r0.x
    mul r1.z, r1.z, r1.z
    mul r1.y, r1.y, r1.z
    utof r1.z, cb0[0].z
    div r1.y, r1.y, r1.z
    sincos r2.x, r3.x, r1.y
    mov r1.y, -r2.x
    store_structured u0.x, r0.y, l(0), r3.x
    store_structured u0.x, r0.w, l(0), r2.x
    store_structured u1.x, r0.y, l(0), r3.x
    store_structured u1.x, r0.w, l(0), r1.y
  else 
    iadd r1.y, r0.z, -cb0[0].z
    iadd r1.y, r1.y, l(1)
    uge r1.y, r0.x, r1.y
    ult r1.z, r0.x, r0.z
    and r1.y, r1.z, r1.y
    if_nz r1.y
      iadd r0.x, -r0.x, r0.z
      movc r0.z, r1.x, l(3.141593), l(-3.141593)
      utof r0.x, r0.x
      mul r0.x, r0.x, r0.x
      mul r0.x, r0.z, r0.x
      utof r0.z, cb0[0].z
      div r0.x, r0.x, r0.z
      sincos null, r0.z, r0.x
      sincos r0.x, null, -r0.x
      store_structured u1.x, r0.y, l(0), r0.z
      store_structured u1.x, r0.w, l(0), r0.x
    else 
      store_structured u1.x, r0.y, l(0), l(0)
      store_structured u1.x, r0.w, l(0), l(0)
    endif 
  endif 
endif 
ret 
// Approximately 0 instruction slots used
#endif

const BYTE g_BluesteinZChirp[] =
{
     68,  88,  66,  67, 237, 104, 
    222, 255,  94,  46,  57, 112, 
     99, 117,  92, 206,  48, 139, 
     62,  51,   1,   0,   0,   0, 
    208,   5,   0,   0,   3,   0, 
      0,   0,  44,   0,   0,   0, 
     60,   0,   0,   0,  76,   0, 
      0,   0,  73,  83,  71,  78, 
      8,   0,   0,   0,   0,   0, 
      0,   0,   8,   0,   0,   0, 
     79,  83,  71,  78,   8,   0, 
      0,   0,   0,   0,   0,   0, 
      8,   0,   0,   0,  83,  72, 
     69,  88, 124,   5,   0,   0, 
     80,   0,   5,   0,  95,   1, 
      0,   0, 106,   8,   0,   1, 
     89,   0,   0,   4,  70, 142, 
     32,   0,   0,   0,   0,   0, 
      1,   0,   0,   0, 158,   0, 
      0,   4,   0, 224,  17,   0, 
      0,   0,   0,   0,   4,   0, 
      0,   0, 158,   0,   0,   4, 
      0, 224,  17,   0,   1,   0, 
      0,   0,   4,   0,   0,   0, 
     95,   0,   0,   2,  18,   0, 
      2,   0, 104,   0,   0,   2, 
      4,   0,   0,   0, 155,   0, 
      0,   4,  64,   0,   0,   0, 
      1,   0,   0,   0,   1,   0, 
      0,   0,  30,   0,   0,   7, 
     18,   0,  16,   0,   0,   0, 
      0,   0,  10,   0,   2,   0, 
     10, 128,  32,   0,   0,   0, 
      0,   0,   0,   0,   0,   0, 
     79,   0,   0,   8,  34,   0, 
     16,   0,   0,   0,   0,   0, 
     10,   0,  16,   0,   0,   0, 
      0,   0,  26, 128,  32,   0, 
      0,   0,   0,   0,   0,   0, 
      0,   0,  31,   0,   4,   3, 
     26,   0,  16,   0,   0,   0, 
      0,   0,  41,   0,   0,   8, 
     34,   0,  16,   0,   0,   0, 
      0,   0,  42, 128,  32,   0, 
      0,   0,   0,   0,   0,   0, 
      0,   0,   1,  64,   0,   0, 
      1,   0,   0,   0,  30,   0, 
      0,   7,  34,   0,  16,   0, 
      0,   0,   0,   0,  26,   0, 
     16,   0,   0,   0,   0,   0, 
      1,  64,   0,   0, 254, 255, 
    255, 255,  54,   0,   0,   5, 
     66,   0,  16,   0,   0,   0, 
      0,   0,   1,  64,   0,   0, 
      1,   0,   0,   0,  48,   0, 
      0,   1,  79,   0,   0,   7, 
    130,   0,  16,   0,   0,   0, 
      0,   0,  26,   0,  16,   0, 
      0,   0,   0,   0,  42,   0, 
     16,   0,   0,   0,   0,   0, 
      3,   0,   4,   3,  58,   0, 
     16,   0,   0,   0,   0,   0, 
     41,   0,   0,   7,  66,   0, 
     16,   0,   0,   0,   0,   0, 
     42,   0,  16,   0,   0,   0, 
      0,   0,   1,  64,   0,   0, 
      1,   0,   0,   0,  22,   0, 
      0,   1,  41,   0,   0,   7, 
     34,   0,  16,   0,   0,   0, 
      0,   0,  10,   0,  16,   0, 
      0,   0,   0,   0,   1,  64, 
      0,   0,   1,   0,   0,   0, 
    140,   0,   0,  11, 130,   0, 
     16,   0,   0,   0,   0,   0, 
      1,  64,   0,   0,  31,   0, 
      0,   0,   1,  64,   0,   0, 
      1,   0,   0,   0,  10,   0, 
     16,   0,   0,   0,   0,   0, 
      1,  64,   0,   0,   1,   0, 
      0,   0,  32,   0,   0,   8, 
     18,   0,  16,   0,   1,   0, 
      0,   0,  58, 128,  32,   0, 
      0,   0,   0,   0,   0,   0, 
      0,   0,   1,  64,   0,   0, 
      1,   0,   0,   0,  79,   0, 
      0,   8,  34,   0,  16,   0, 
      1,   0,   0,   0,  10,   0, 
     16,   0,   0,   0,   0,   0, 
     42, 128,  32,   0,   0,   0, 
      0,   0,   0,   0,   0,   0, 
     31,   0,   4,   3,  26,   0, 
     16,   0,   1,   0,   0,   0, 
     55,   0,   0,   9,  34,   0, 
     16,   0,   1,   0,   0,   0, 
     10,   0,  16,   0,   1,   0, 
      0,   0,   1,  64,   0,   0, 
    219,  15,  73,  64,   1,  64, 
      0,   0, 219,  15,  73, 192, 
     86,   0,   0,   5,  66,   0, 
     16,   0,   1,   0,   0,   0, 
     10,   0,  16,   0,   0,   0, 
      0,   0,  56,   0,   0,   7, 
     66,   0,  16,   0,   1,   0, 
      0,   0,  42,   0,  16,   0, 
      1,   0,   0,   0,  42,   0, 
     16,   0,   1,   0,   0,   0, 
     56,   0,   0,   7,  34,   0, 
     16,   0,   1,   0,   0,   0, 
     26,   0,  16,   0,   1,   0, 
      0,   0,  42,   0,  16,   0, 
      1,   0,   0,   0,  86,   0, 
      0,   6,  66,   0,  16,   0, 
      1,   0,   0,   0,  42, 128, 
     32,   0,   0,   0,   0,   0, 
      0,   0,   0,   0,  14,   0, 
      0,   7,  34,   0,  16,   0, 
      1,   0,   0,   0,  26,   0, 
     16,   0,   1,   0,   0,   0, 
     42,   0,  16,   0,   1,   0, 
      0,   0,  77,   0,   0,   7, 
     18,   0,  16,   0,   2,   0, 
      0,   0,  18,   0,  16,   0, 
      3,   0,   0,   0,  26,   0, 
     16,   0,   1,   0,   0,   0, 
     54,   0,   0,   6,  34,   0, 
     16,   0,   1,   0,   0,   0, 
     10,   0,  16, 128,  65,   0, 
      0,   0,   2,   0,   0,   0, 
    168,   0,   0,   9,  18, 224, 
     17,   0,   0,   0,   0,   0, 
     26,   0,  16,   0,   0,   0, 
      0,   0,   1,  64,   0,   0, 
      0,   0,   0,   0,  10,   0, 
     16,   0,   3,   0,   0,   0, 
    168,   0,   0,   9,  18, 224, 
     17,   0,   0,   0,   0,   0, 
     58,   0,  16,   0,   0,   0, 
      0,   0,   1,  64,   0,   0, 
      0,   0,   0,   0,  10,   0, 
     16,   0,   2,   0,   0,   0, 
    168,   0,   0,   9,  18, 224, 
     17,   0,   1,   0,   0,   0, 
     26,   0,  16,   0,   0,   0, 
      0,   0,   1,  64,   0,   0, 
      0,   0,   0,   0,  10,   0, 
     16,   0,   3,   0,   0,   0, 
    168,   0,   0,   9,  18, 224, 
     17,   0,   1,   0,   0,   0, 
     58,   0,  16,   0,   0,   0, 
      0,   0,   1,  64,   0,   0, 
      0,   0,   0,   0,  26,   0, 
     16,   0,   1,   0,   0,   0, 
     18,   0,   0,   1,  30,   0, 
      0,   9,  34,   0,  16,   0, 
      1,   0,   0,   0,  42,   0, 
     16,   0,   0,   0,   0,   0, 
     42, 128,  32, 128,  65,   0, 
      0,   0,   0,   0,   0,   0, 
      0,   0,   0,   0,  30,   0, 
      0,   7,  34,   0,  16,   0, 
      1,   0,   0,   0,  26,   0, 
     16,   0,   1,   0,   0,   0, 
      1,  64,   0,   0,   1,   0, 
      0,   0,  80,   0,   0,   7, 
     34,   0,  16,   0,   1,   0, 
      0,   0,  10,   0,  16,   0, 
      0,   0,   0,   0,  26,   0, 
     16,   0,   1,   0,   0,   0, 
     79,   0,   0,   7,  66,   0, 
     16,   0,   1,   0,   0,   0, 
     10,   0,  16,   0,   0,   0, 
      0,   0,  42,   0,  16,   0, 
      0,   0,   0,   0,   1,   0, 
      0,   7,  34,   0,  16,   0, 
      1,   0,   0,   0,  42,   0, 
     16,   0,   1,   0,   0,   0, 
     26,   0,  16,   0,   1,   0, 
      0,   0,  31,   0,   4,   3, 
     26,   0,  16,   0,   1,   0, 
      0,   0,  30,   0,   0,   8, 
     18,   0,  16,   0,   0,   0, 
      0,   0,  10,   0,  16, 128, 
     65,   0,   0,   0,   0,   0, 
      0,   0,  42,   0,  16,   0, 
      0,   0,   0,   0,  55,   0, 
      0,   9,  66,   0,  16,   0, 
      0,   0,   0,   0,  10,   0, 
     16,   0,   1,   0,   0,   0, 
      1,  64,   0,   0, 219,  15, 
     73,  64,   1,  64,   0,   0, 
    219,  15,  73, 192,  86,   0, 
      0,   5,  18,   0,  16,   0, 
      0,   0,   0,   0,  10,   0, 
     16,   0,   0,   0,   0,   0, 
     56,   0,   0,   7,  18,   0, 
     16,   0,   0,   0,   0,   0, 
     10,   0,  16,   0,   0,   0, 
      0,   0,  10,   0,  16,   0, 
      0,   0,   0,   0,  56,   0, 
      0,   7,  18,   0,  16,   0, 
      0,   0,   0,   0,  42,   0, 
     16,   0,   0,   0,   0,   0, 
     10,   0,  16,   0,   0,   0, 
      0,   0,  86,   0,   0,   6, 
     66,   0,  16,   0,   0,   0, 
      0,   0,  42, 128,  32,   0, 
      0,   0,   0,   0,   0,   0, 
      0,   0,  14,   0,   0,   7, 
     18,   0,  16,   0,   0,   0, 
      0,   0,  10,   0,  16,   0, 
      0,   0,   0,   0,  42,   0, 
     16,   0,   0,   0,   0,   0, 
     77,   0,   0,   6,   0, 208, 
      0,   0,  66,   0,  16,   0, 
      0,   0,   0,   0,  10,   0, 
     16,   0,   0,   0,   0,   0, 
     77,   0,   0,   7,  18,   0, 
     16,   0,   0,   0,   0,   0, 
      0, 208,   0,   0,  10,   0, 
     16, 128,  65,   0,   0,   0, 
      0,   0,   0,   0, 168,   0, 
      0,   9,  18, 224,  17,   0, 
      1,   0,   0,   0,  26,   0, 
     16,   0,   0,   0,   0,   0, 
      1,  64,   0,   0,   0,   0, 
      0,   0,  42,   0,  16,   0, 
      0,   0,   0,   0, 168,   0, 
      0,   9,  18, 224,  17,   0, 
      1,   0,   0,   0,  58,   0, 
     16,   0,   0,   0,   0,   0, 
      1,  64,   0,   0,   0,   0, 
      0,   0,  10,   0,  16,   0, 
      0,   0,   0,   0,  18,   0, 
      0,   1, 168,   0,   0,   9, 
     18, 224,  17,   0,   1,   0, 
      0,   0,  26,   0,  16,   0, 
      0,   0,   0,   0,   1,  64, 
      0,   0,   0,   0,   0,   0, 
      1,  64,   0,   0,   0,   0, 
      0,   0, 168,   0,   0,   9, 
     18, 224,  17,   0,   1,   0, 
      0,   0,  58,   0,  16,   0, 
      0,   0,   0,   0,   1,  64, 
      0,   0,   0,   0,   0,   0, 
      1,  64,   0,   0,   0,   0, 
      0,   0,  21,   0,   0,   1, 
     21,   0,   0,   1,  21,   0, 
      0,   1,  62,   0,   0,   1
};
