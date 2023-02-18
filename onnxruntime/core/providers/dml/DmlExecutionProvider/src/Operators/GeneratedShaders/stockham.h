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
dcl_constantbuffer CB0[6], immediateIndexed
dcl_uav_structured u0, 4
dcl_uav_structured u1, 4
dcl_input vThreadID.x
dcl_temps 4
dcl_thread_group 64, 1, 1
iadd r0.x, vThreadID.x, cb0[0].x
ult r0.y, r0.x, cb0[0].y
if_nz r0.y
  ushr r0.y, cb0[5].y, l(1)
  ishl r0.z, l(1), cb0[0].z
  iadd r0.w, cb0[0].z, l(-1)
  ishl r0.w, l(1), r0.w
  imul null, r1.x, cb0[3].z, cb0[3].y
  udiv r1.x, r2.x, r0.x, r1.x
  udiv r2.x, r3.x, r2.x, cb0[3].z
  ushr r1.y, r2.x, cb0[0].z
  udiv null, r1.z, r2.x, r0.w
  imad r0.w, r1.y, r0.w, r1.z
  iadd r0.y, r0.y, r0.w
  imul null, r1.x, r1.x, cb0[2].x
  imad r0.w, r0.w, cb0[2].y, r1.x
  imad r0.w, r3.x, cb0[2].z, r0.w
  ld_structured_indexable(structured_buffer, stride=4)(mixed,mixed,mixed,mixed) r1.y, r0.w, l(0), u0.xxxx
  ieq r1.z, cb0[1].w, l(2)
  iadd r0.w, r0.w, cb0[2].w
  ld_structured_indexable(structured_buffer, stride=4)(mixed,mixed,mixed,mixed) r0.w, r0.w, l(0), u0.xxxx
  imad r0.y, r0.y, cb0[2].y, r1.x
  imad r0.y, r3.x, cb0[2].z, r0.y
  ld_structured_indexable(structured_buffer, stride=4)(mixed,mixed,mixed,mixed) r1.x, r0.y, l(0), u0.xxxx
  iadd r0.y, r0.y, cb0[2].w
  ld_structured_indexable(structured_buffer, stride=4)(mixed,mixed,mixed,mixed) r0.y, r0.y, l(0), u0.xxxx
  and r0.yw, r0.yyyw, r1.zzzz
  udiv null, r1.z, r2.x, r0.z
  ieq r1.w, cb0[0].w, l(1)
  movc r1.w, r1.w, l(6.283185), l(-6.283185)
  utof r1.z, r1.z
  mul r1.z, r1.z, r1.w
  utof r0.z, r0.z
  div r0.z, r1.z, r0.z
  sincos r2.x, r3.x, r0.z
  imul null, r0.z, r0.x, cb0[4].z
  imad r0.x, r0.x, cb0[4].z, cb0[4].w
  mul r1.z, r0.y, r2.x
  mad r1.z, r3.x, r1.x, -r1.z
  add r1.y, r1.z, r1.y
  mul r1.y, r1.y, cb0[5].x
  store_structured u1.x, r0.z, l(0), r1.y
  mul r0.z, r1.x, r2.x
  mad r0.y, r3.x, r0.y, r0.z
  add r0.y, r0.y, r0.w
  mul r0.y, r0.y, cb0[5].x
  store_structured u1.x, r0.x, l(0), r0.y
endif 
ret 
// Approximately 0 instruction slots used
#endif

const BYTE g_DFT[] =
{
     68,  88,  66,  67,  58, 203, 
     16,  51, 250, 104, 115,  86, 
    213, 141,  90, 165, 153,  48, 
     29, 102,   1,   0,   0,   0, 
    212,   6,   0,   0,   3,   0, 
      0,   0,  44,   0,   0,   0, 
     60,   0,   0,   0,  76,   0, 
      0,   0,  73,  83,  71,  78, 
      8,   0,   0,   0,   0,   0, 
      0,   0,   8,   0,   0,   0, 
     79,  83,  71,  78,   8,   0, 
      0,   0,   0,   0,   0,   0, 
      8,   0,   0,   0,  83,  72, 
     69,  88, 128,   6,   0,   0, 
     80,   0,   5,   0, 160,   1, 
      0,   0, 106,   8,   0,   1, 
     89,   0,   0,   4,  70, 142, 
     32,   0,   0,   0,   0,   0, 
      6,   0,   0,   0, 158,   0, 
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
      0,   0,  85,   0,   0,   8, 
     34,   0,  16,   0,   0,   0, 
      0,   0,  26, 128,  32,   0, 
      0,   0,   0,   0,   5,   0, 
      0,   0,   1,  64,   0,   0, 
      1,   0,   0,   0,  41,   0, 
      0,   8,  66,   0,  16,   0, 
      0,   0,   0,   0,   1,  64, 
      0,   0,   1,   0,   0,   0, 
     42, 128,  32,   0,   0,   0, 
      0,   0,   0,   0,   0,   0, 
     30,   0,   0,   8, 130,   0, 
     16,   0,   0,   0,   0,   0, 
     42, 128,  32,   0,   0,   0, 
      0,   0,   0,   0,   0,   0, 
      1,  64,   0,   0, 255, 255, 
    255, 255,  41,   0,   0,   7, 
    130,   0,  16,   0,   0,   0, 
      0,   0,   1,  64,   0,   0, 
      1,   0,   0,   0,  58,   0, 
     16,   0,   0,   0,   0,   0, 
     38,   0,   0,  10,   0, 208, 
      0,   0,  18,   0,  16,   0, 
      1,   0,   0,   0,  42, 128, 
     32,   0,   0,   0,   0,   0, 
      3,   0,   0,   0,  26, 128, 
     32,   0,   0,   0,   0,   0, 
      3,   0,   0,   0,  78,   0, 
      0,   9,  18,   0,  16,   0, 
      1,   0,   0,   0,  18,   0, 
     16,   0,   2,   0,   0,   0, 
     10,   0,  16,   0,   0,   0, 
      0,   0,  10,   0,  16,   0, 
      1,   0,   0,   0,  78,   0, 
      0,  10,  18,   0,  16,   0, 
      2,   0,   0,   0,  18,   0, 
     16,   0,   3,   0,   0,   0, 
     10,   0,  16,   0,   2,   0, 
      0,   0,  42, 128,  32,   0, 
      0,   0,   0,   0,   3,   0, 
      0,   0,  85,   0,   0,   8, 
     34,   0,  16,   0,   1,   0, 
      0,   0,  10,   0,  16,   0, 
      2,   0,   0,   0,  42, 128, 
     32,   0,   0,   0,   0,   0, 
      0,   0,   0,   0,  78,   0, 
      0,   8,   0, 208,   0,   0, 
     66,   0,  16,   0,   1,   0, 
      0,   0,  10,   0,  16,   0, 
      2,   0,   0,   0,  58,   0, 
     16,   0,   0,   0,   0,   0, 
     35,   0,   0,   9, 130,   0, 
     16,   0,   0,   0,   0,   0, 
     26,   0,  16,   0,   1,   0, 
      0,   0,  58,   0,  16,   0, 
      0,   0,   0,   0,  42,   0, 
     16,   0,   1,   0,   0,   0, 
     30,   0,   0,   7,  34,   0, 
     16,   0,   0,   0,   0,   0, 
     26,   0,  16,   0,   0,   0, 
      0,   0,  58,   0,  16,   0, 
      0,   0,   0,   0,  38,   0, 
      0,   9,   0, 208,   0,   0, 
     18,   0,  16,   0,   1,   0, 
      0,   0,  10,   0,  16,   0, 
      1,   0,   0,   0,  10, 128, 
     32,   0,   0,   0,   0,   0, 
      2,   0,   0,   0,  35,   0, 
      0,  10, 130,   0,  16,   0, 
      0,   0,   0,   0,  58,   0, 
     16,   0,   0,   0,   0,   0, 
     26, 128,  32,   0,   0,   0, 
      0,   0,   2,   0,   0,   0, 
     10,   0,  16,   0,   1,   0, 
      0,   0,  35,   0,   0,  10, 
    130,   0,  16,   0,   0,   0, 
      0,   0,  10,   0,  16,   0, 
      3,   0,   0,   0,  42, 128, 
     32,   0,   0,   0,   0,   0, 
      2,   0,   0,   0,  58,   0, 
     16,   0,   0,   0,   0,   0, 
    167,   0,   0, 139,   2,  35, 
      0, 128, 131, 153,  25,   0, 
     34,   0,  16,   0,   1,   0, 
      0,   0,  58,   0,  16,   0, 
      0,   0,   0,   0,   1,  64, 
      0,   0,   0,   0,   0,   0, 
      6, 224,  17,   0,   0,   0, 
      0,   0,  32,   0,   0,   8, 
     66,   0,  16,   0,   1,   0, 
      0,   0,  58, 128,  32,   0, 
      0,   0,   0,   0,   1,   0, 
      0,   0,   1,  64,   0,   0, 
      2,   0,   0,   0,  30,   0, 
      0,   8, 130,   0,  16,   0, 
      0,   0,   0,   0,  58,   0, 
     16,   0,   0,   0,   0,   0, 
     58, 128,  32,   0,   0,   0, 
      0,   0,   2,   0,   0,   0, 
    167,   0,   0, 139,   2,  35, 
      0, 128, 131, 153,  25,   0, 
    130,   0,  16,   0,   0,   0, 
      0,   0,  58,   0,  16,   0, 
      0,   0,   0,   0,   1,  64, 
      0,   0,   0,   0,   0,   0, 
      6, 224,  17,   0,   0,   0, 
      0,   0,  35,   0,   0,  10, 
     34,   0,  16,   0,   0,   0, 
      0,   0,  26,   0,  16,   0, 
      0,   0,   0,   0,  26, 128, 
     32,   0,   0,   0,   0,   0, 
      2,   0,   0,   0,  10,   0, 
     16,   0,   1,   0,   0,   0, 
     35,   0,   0,  10,  34,   0, 
     16,   0,   0,   0,   0,   0, 
     10,   0,  16,   0,   3,   0, 
      0,   0,  42, 128,  32,   0, 
      0,   0,   0,   0,   2,   0, 
      0,   0,  26,   0,  16,   0, 
      0,   0,   0,   0, 167,   0, 
      0, 139,   2,  35,   0, 128, 
    131, 153,  25,   0,  18,   0, 
     16,   0,   1,   0,   0,   0, 
     26,   0,  16,   0,   0,   0, 
      0,   0,   1,  64,   0,   0, 
      0,   0,   0,   0,   6, 224, 
     17,   0,   0,   0,   0,   0, 
     30,   0,   0,   8,  34,   0, 
     16,   0,   0,   0,   0,   0, 
     26,   0,  16,   0,   0,   0, 
      0,   0,  58, 128,  32,   0, 
      0,   0,   0,   0,   2,   0, 
      0,   0, 167,   0,   0, 139, 
      2,  35,   0, 128, 131, 153, 
     25,   0,  34,   0,  16,   0, 
      0,   0,   0,   0,  26,   0, 
     16,   0,   0,   0,   0,   0, 
      1,  64,   0,   0,   0,   0, 
      0,   0,   6, 224,  17,   0, 
      0,   0,   0,   0,   1,   0, 
      0,   7, 162,   0,  16,   0, 
      0,   0,   0,   0,  86,  13, 
     16,   0,   0,   0,   0,   0, 
    166,  10,  16,   0,   1,   0, 
      0,   0,  78,   0,   0,   8, 
      0, 208,   0,   0,  66,   0, 
     16,   0,   1,   0,   0,   0, 
     10,   0,  16,   0,   2,   0, 
      0,   0,  42,   0,  16,   0, 
      0,   0,   0,   0,  32,   0, 
      0,   8, 130,   0,  16,   0, 
      1,   0,   0,   0,  58, 128, 
     32,   0,   0,   0,   0,   0, 
      0,   0,   0,   0,   1,  64, 
      0,   0,   1,   0,   0,   0, 
     55,   0,   0,   9, 130,   0, 
     16,   0,   1,   0,   0,   0, 
     58,   0,  16,   0,   1,   0, 
      0,   0,   1,  64,   0,   0, 
    219,  15, 201,  64,   1,  64, 
      0,   0, 219,  15, 201, 192, 
     86,   0,   0,   5,  66,   0, 
     16,   0,   1,   0,   0,   0, 
     42,   0,  16,   0,   1,   0, 
      0,   0,  56,   0,   0,   7, 
     66,   0,  16,   0,   1,   0, 
      0,   0,  42,   0,  16,   0, 
      1,   0,   0,   0,  58,   0, 
     16,   0,   1,   0,   0,   0, 
     86,   0,   0,   5,  66,   0, 
     16,   0,   0,   0,   0,   0, 
     42,   0,  16,   0,   0,   0, 
      0,   0,  14,   0,   0,   7, 
     66,   0,  16,   0,   0,   0, 
      0,   0,  42,   0,  16,   0, 
      1,   0,   0,   0,  42,   0, 
     16,   0,   0,   0,   0,   0, 
     77,   0,   0,   7,  18,   0, 
     16,   0,   2,   0,   0,   0, 
     18,   0,  16,   0,   3,   0, 
      0,   0,  42,   0,  16,   0, 
      0,   0,   0,   0,  38,   0, 
      0,   9,   0, 208,   0,   0, 
     66,   0,  16,   0,   0,   0, 
      0,   0,  10,   0,  16,   0, 
      0,   0,   0,   0,  42, 128, 
     32,   0,   0,   0,   0,   0, 
      4,   0,   0,   0,  35,   0, 
      0,  11,  18,   0,  16,   0, 
      0,   0,   0,   0,  10,   0, 
     16,   0,   0,   0,   0,   0, 
     42, 128,  32,   0,   0,   0, 
      0,   0,   4,   0,   0,   0, 
     58, 128,  32,   0,   0,   0, 
      0,   0,   4,   0,   0,   0, 
     56,   0,   0,   7,  66,   0, 
     16,   0,   1,   0,   0,   0, 
     26,   0,  16,   0,   0,   0, 
      0,   0,  10,   0,  16,   0, 
      2,   0,   0,   0,  50,   0, 
      0,  10,  66,   0,  16,   0, 
      1,   0,   0,   0,  10,   0, 
     16,   0,   3,   0,   0,   0, 
     10,   0,  16,   0,   1,   0, 
      0,   0,  42,   0,  16, 128, 
     65,   0,   0,   0,   1,   0, 
      0,   0,   0,   0,   0,   7, 
     34,   0,  16,   0,   1,   0, 
      0,   0,  42,   0,  16,   0, 
      1,   0,   0,   0,  26,   0, 
     16,   0,   1,   0,   0,   0, 
     56,   0,   0,   8,  34,   0, 
     16,   0,   1,   0,   0,   0, 
     26,   0,  16,   0,   1,   0, 
      0,   0,  10, 128,  32,   0, 
      0,   0,   0,   0,   5,   0, 
      0,   0, 168,   0,   0,   9, 
     18, 224,  17,   0,   1,   0, 
      0,   0,  42,   0,  16,   0, 
      0,   0,   0,   0,   1,  64, 
      0,   0,   0,   0,   0,   0, 
     26,   0,  16,   0,   1,   0, 
      0,   0,  56,   0,   0,   7, 
     66,   0,  16,   0,   0,   0, 
      0,   0,  10,   0,  16,   0, 
      1,   0,   0,   0,  10,   0, 
     16,   0,   2,   0,   0,   0, 
     50,   0,   0,   9,  34,   0, 
     16,   0,   0,   0,   0,   0, 
     10,   0,  16,   0,   3,   0, 
      0,   0,  26,   0,  16,   0, 
      0,   0,   0,   0,  42,   0, 
     16,   0,   0,   0,   0,   0, 
      0,   0,   0,   7,  34,   0, 
     16,   0,   0,   0,   0,   0, 
     26,   0,  16,   0,   0,   0, 
      0,   0,  58,   0,  16,   0, 
      0,   0,   0,   0,  56,   0, 
      0,   8,  34,   0,  16,   0, 
      0,   0,   0,   0,  26,   0, 
     16,   0,   0,   0,   0,   0, 
     10, 128,  32,   0,   0,   0, 
      0,   0,   5,   0,   0,   0, 
    168,   0,   0,   9,  18, 224, 
     17,   0,   1,   0,   0,   0, 
     10,   0,  16,   0,   0,   0, 
      0,   0,   1,  64,   0,   0, 
      0,   0,   0,   0,  26,   0, 
     16,   0,   0,   0,   0,   0, 
     21,   0,   0,   1,  62,   0, 
      0,   1
};
