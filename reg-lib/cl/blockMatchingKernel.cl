//To enable double precision
#if defined(cl_khr_fp64)  // Khronos extension available?
#pragma OPENCL EXTENSION cl_khr_fp64 : enable
#define DOUBLE_SUPPORT_AVAILABLE
#elif defined(cl_amd_fp64)  // AMD extension available?
#pragma OPENCL EXTENSION cl_amd_fp64 : enable
#define DOUBLE_SUPPORT_AVAILABLE
#else
#warning "double precision floating point not supported by OpenCL implementation.";
#endif

#if defined(DOUBLE_SUPPORT_AVAILABLE)

// double
typedef double real_t;
typedef double2 real2_t;
typedef double3 real3_t;
typedef double4 real4_t;
typedef double8 real8_t;
typedef double16 real16_t;
#define PI 3.14159265358979323846

#else

// float
typedef float real_t;
typedef float2 real2_t;
typedef float3 real3_t;
typedef float4 real4_t;
typedef float8 real8_t;
typedef float16 real16_t;
#define PI 3.14159265359f

#endif


#define REDUCE reduceCustom
#define REDUCE2D reduce2DCustom
#define BLOCK_WIDTH 4


/* *************************************************************** */
/* *************************************************************** */
__inline__
void reg2D_mat44_mul_cl(__global float* mat,
                        float const* in,
                        __global float *out)
{
                out[0] = (float) ((real_t) mat[0 * 4 + 0] * (real_t) in[0] +
                         (real_t) mat[0 * 4 + 1] * (real_t) in[1] +
                         (real_t) mat[0 * 4 + 2] * (real_t) 0 +
                         (real_t) mat[0 * 4 + 3]);
                out[1] = (float) ((real_t) mat[1 * 4 + 0] * (real_t) in[0] +
                         (real_t) mat[1 * 4 + 1] * (real_t) in[1] +
                         (real_t) mat[1 * 4 + 2] * (real_t) 0 +
                         (real_t) mat[1 * 4 + 3]);
}
/* *************************************************************** */
/* *************************************************************** */
__inline__
void reg_mat44_mul_cl(__global float* mat,
                      float const* in,
                    __global float *out)
{
            out[0] = (float) ((real_t) mat[0 * 4 + 0] * (real_t) in[0] +
                     (real_t) mat[0 * 4 + 1] * (real_t) in[1] +
                     (real_t) mat[0 * 4 + 2] * (real_t) in[2] +
                     (real_t) mat[0 * 4 + 3]);
            out[1] = (float) ((real_t) mat[1 * 4 + 0] * (real_t) in[0] +
                     (real_t) mat[1 * 4 + 1] * (real_t) in[1] +
                     (real_t) mat[1 * 4 + 2] * (real_t) in[2] +
                     (real_t) mat[1 * 4 + 3]);
            out[2] = (float) ((real_t) mat[2 * 4 + 0] * (real_t) in[0] +
                         (real_t) mat[2 * 4 + 1] * (real_t) in[1] +
                         (real_t) mat[2 * 4 + 2] * (real_t) in[2] +
                         (real_t) mat[2 * 4 + 3]);
}
/* *************************************************************** */
/* *************************************************************** */
__inline__ real_t reduce2DCustom(__local float* sData2,
                                         float data,
                                         const unsigned int tid)
{
		sData2[tid] = data;
		barrier(CLK_LOCAL_MEM_FENCE);

		for (int i = 8; i > 0; i >>= 1){
				if (tid < i) sData2[tid] += sData2[tid + i];
				barrier(CLK_LOCAL_MEM_FENCE);
		}

                const real_t temp = sData2[0];
		barrier(CLK_LOCAL_MEM_FENCE);

		return temp;
}
/* *************************************************************** */
/* *************************************************************** */
__inline__ real_t reduceCustom(__local float* sData2,
                                       float data,
                                       const unsigned int tid)
{
		sData2[tid] = data;
		barrier(CLK_LOCAL_MEM_FENCE);

		for (int i = 32; i > 0; i >>= 1){
				if (tid < i) sData2[tid] += sData2[tid + i];
				barrier(CLK_LOCAL_MEM_FENCE);
		}

                const real_t temp = sData2[0];
		barrier(CLK_LOCAL_MEM_FENCE);

		return temp;
}
/* *************************************************************** */
/* *************************************************************** */
__kernel void blockMatchingKernel2D(__local  float* sResultValues,
                                    __global float* warpedImageArray,
                                    __global float* referenceImageArray,
                                    __global float *warpedPosition,
                                    __global float *referencePosition,
                                    __global int *totalBlock,
                                    __global int* mask,
                                    __global float* referenceMatrix_xyz,
                                    __global int* definedBlock,
                                    uint3 c_ImageSize,
                                    const int blocksRange,
                                    const unsigned int stepSize)
{

                const uint numBlocks = blocksRange * 2 + 1;

                __local float sData[16];
                for(int i=0; i<16; ++i) sData[i] = 0;

                const unsigned int idx = get_local_id(0);
                const unsigned int idy = get_local_id(1);

                const unsigned int bid = get_group_id(0) + get_num_groups(0) * get_group_id(1);

                const unsigned int xBaseImage = get_group_id(0) * 4;
                const unsigned int yBaseImage = get_group_id(1) * 4;

                const unsigned int tid = idy * 4 + idx;

                const unsigned int xImage = xBaseImage + idx;
                const unsigned int yImage = yBaseImage + idy;

                const unsigned long imgIdx = xImage + yImage *(c_ImageSize.x);
                const bool referenceInBounds = xImage < c_ImageSize.x && yImage < c_ImageSize.y;

                const int currentBlockIndex = totalBlock[bid];

                __global float* start_warpedPosition = &warpedPosition[0];
                __global float* start_referencePosition = &referencePosition[0];

                if (currentBlockIndex > -1){

                    float bestDisplacement[3] = { NAN, 0.0f, 0.0f };
                    float bestCC = blocksRange > 1 ? 0.9f : 0.0f;

                    //populate shared memory with warpedImageArray's values
                    for (int m = -1 * blocksRange; m <= blocksRange; m += 1) {
                        for (int l = -1 * blocksRange; l <= blocksRange; l += 1) {
                            const int x = l * 4 + idx;
                            const int y = m * 4 + idy;

                            const unsigned int sIdx = (y + blocksRange * 4) * numBlocks * 4 + (x + blocksRange * 4);

                            const int xImageIn = xBaseImage + x;
                            const int yImageIn = yBaseImage + y;

                            const int indexXYZIn = xImageIn + yImageIn *(c_ImageSize.x);

                            const bool valid = (xImageIn >= 0 && xImageIn < c_ImageSize.x) && (yImageIn >= 0 && yImageIn < c_ImageSize.y);
                            sWarpedValues[sIdx] = (valid && mask[indexXYZIn] > -1) ? warpedImageArray[indexXYZIn] : NAN;

                        }
                    }

                            float rReferenceValue = (referenceInBounds && mask[imgIdx] > -1) ? referenceImageArray[imgIdx] : NAN;
                            const bool finiteReference = isfinite(rReferenceValue);
                            rReferenceValue = finiteReference ? rReferenceValue : 0.0f;

                            const unsigned int referenceSize = REDUCE2D(sData, finiteReference ? 1.0f : 0.0f, tid);

                            if (referenceSize > 8){

                                    const float referenceMean = REDUCE2D(sData, rReferenceValue, tid) / referenceSize;
                                    const float referenceTemp = finiteReference ? rReferenceValue - referenceMean : 0.0f;
                                    const float referenceVar = REDUCE2D(sData, referenceTemp*referenceTemp, tid);

                                    // iteration over the warped blocks (block matching part)
                                    for (unsigned int m = 1; m < blocksRange * 8 /*2*4*/; m += stepSize) {
                                        for (unsigned int l = 1; l < blocksRange * 8 /*2*4*/; l += stepSize) {

                                            const unsigned int sIdxIn = (idy + m) * numBlocks * 4 + idx + l;

                                            const float rWarpedValue = sWarpedValues[sIdxIn];
                                            const bool overlap = isfinite(rWarpedValue) && finiteReference;
                                            const unsigned int bSize = REDUCE2D(sData, overlap ? 1.0f : 0.0f, tid);

                                            if (bSize > 8){
                                                float newReferenceTemp = referenceTemp;
                                                float newReferencevar = referenceVar;
                                                    if (bSize != referenceSize){
                                                        const float newReferenceValue = overlap ? rReferenceValue : 0.0f;
                                                        const float newargetMean = REDUCE2D(sData, newReferenceValue, tid) / bSize;
                                                        newReferenceTemp = overlap ? newReferenceValue - newargetMean : 0.0f;
                                                        newReferencevar = REDUCE2D(sData, newReferenceTemp*newReferenceTemp, tid);
                                                    }

                                                    const float rChecked = overlap ? rWarpedValue : 0.0f;
                                                    const float warpedMean = REDUCE2D(sData, rChecked, tid) / bSize;
                                                    const float warpedTemp = overlap ? rWarpedValue - warpedMean : 0.0f;
                                                    const float warpedVar = REDUCE2D(sData, warpedTemp*warpedTemp, tid);

                                                    const float sumReferenceWarped = REDUCE2D(sData, (newReferenceTemp)*(warpedTemp), tid);
                                                    const float localCC = fabs((sumReferenceWarped) / sqrt(newReferencevar*warpedVar));

                                                    if (tid == 0 && localCC > bestCC) {
                                                        bestCC = localCC;
                                                        bestDisplacement[0] = l - 4.0f;
                                                        bestDisplacement[1] = m - 4.0f;
                                                        bestDisplacement[2] = 0;
                                                    }
                                            }
                                       }
                                    }
                            }

                            if (tid == 0 /*&& isfinite(bestDisplacement[0])*/) {
                                const unsigned int posIdx = 2 * currentBlockIndex;

                                referencePosition = start_referencePosition + posIdx;
                                warpedPosition = start_warpedPosition + posIdx;

                                const float referencePosition_temp[3] = { (float)xBaseImage, (float)yBaseImage, 0 };

                                bestDisplacement[0] += referencePosition_temp[0];
                                bestDisplacement[1] += referencePosition_temp[1];
                                bestDisplacement[2] += 0;

                                reg2D_mat44_mul_cl(referenceMatrix_xyz, referencePosition_temp, referencePosition);
                                reg2D_mat44_mul_cl(referenceMatrix_xyz, bestDisplacement, warpedPosition);
                                if (isfinite(bestDisplacement[0])) {
                                        atomic_add(definedBlock, 1);
                            }
                            }
            }
}
/* *************************************************************** */
/* *************************************************************** */
__kernel void blockMatchingKernel3D(__local float *sResultValues,
																						__global float* resultImageArray,
																						__global float* targetImageArray,
																						__global float *warpedPosition,
																						__global float *referencePosition,
																						__global int *totalBlock,
																						__global int* mask,
																						__global float* referenceMatrix_xyz,
																						__global int* definedBlock,
																						uint3 c_ImageSize,
																						const int blocksRange,
																						const unsigned int stepSize)
{

		const uint numBlocks = blocksRange * 2 + 1;

		__local float sData[64];
                for(int i=0; i<64; ++i) sData[i] = 0;

		// Assign the current coordonate of the voxel in the block
		const unsigned int idx = get_local_id(0);
		const unsigned int idy = get_local_id(1);
		const unsigned int idz = get_local_id(2);

		// Compute the current block index
		const unsigned int bid = get_group_id(0) + get_num_groups(0) * get_group_id(1) + (get_num_groups(0) * get_num_groups(1)) * get_group_id(2);

		// Compute the coordinate of the first voxel of the current block
		const unsigned int xBaseImage = get_group_id(0) * 4;
		const unsigned int yBaseImage = get_group_id(1) * 4;
		const unsigned int zBaseImage = get_group_id(2) * 4;

		// Compute the current voxel index in the block
		const unsigned int tid = idz * 16 + idy * 4 + idx;

		// Compute the coordinate of the current voxel in the whole image
		const unsigned int xImage = xBaseImage + idx;
		const unsigned int yImage = yBaseImage + idy;
		const unsigned int zImage = zBaseImage + idz;

		// Compute the index of the current voxel in the whole image
		const unsigned long imgIdx = xImage + yImage *(c_ImageSize.x) + zImage * (c_ImageSize.x * c_ImageSize.y);

		// Define a boolean to check if the current voxel is in the input image space
		const bool targetInBounds = xImage < c_ImageSize.x && yImage < c_ImageSize.y && zImage < c_ImageSize.z;

		// Check the actual index in term of active voxel
		const int currentBlockIndex = totalBlock[bid];

		// Useless variable
		__global float* start_warpedPosition = &warpedPosition[0];
		__global float* start_referencePosition = &referencePosition[0];

		// Check if the current block is active
		if (currentBlockIndex > -1){

				// Define temp variable to store the displacement
				float bestDisplacement[3] = { NAN, 0.0f, 0.0f };
				float bestCC = blocksRange > 1 ? 0.9f : 0.0f;

				//populate shared memory with resultImageArray's values
				for (int n = -1 * blocksRange; n <= blocksRange; n += 1) {
						for (int m = -1 * blocksRange; m <= blocksRange; m += 1) {
								for (int l = -1 * blocksRange; l <= blocksRange; l += 1) {
										const int x = l * 4 + idx;
										const int y = m * 4 + idy;
										const int z = n * 4 + idz;

										const unsigned int sIdx = (z + blocksRange * 4)* numBlocks * 4 * numBlocks * 4 + (y + blocksRange * 4) * numBlocks * 4 + (x + blocksRange * 4);

										const int xImageIn = xBaseImage + x;
										const int yImageIn = yBaseImage + y;
										const int zImageIn = zBaseImage + z;

										const int indexXYZIn = xImageIn + yImageIn *(c_ImageSize.x) + zImageIn * (c_ImageSize.x * c_ImageSize.y);

										const bool valid = (xImageIn >= 0 && xImageIn < c_ImageSize.x) && (yImageIn >= 0 && yImageIn < c_ImageSize.y) && (zImageIn >= 0 && zImageIn < c_ImageSize.z);
										sResultValues[sIdx] = (valid && mask[indexXYZIn] > -1) ? resultImageArray[indexXYZIn] : NAN;

								}
						}
				}

				float rTargetValue = (targetInBounds && mask[imgIdx] > -1) ? targetImageArray[imgIdx] : NAN;
				const bool finiteTarget = isfinite(rTargetValue);
				rTargetValue = finiteTarget ? rTargetValue : 0.0f;

				const unsigned int targetSize = REDUCE(sData, finiteTarget ? 1.0f : 0.0f, tid);

				if (targetSize > 32){

						const float targetMean = REDUCE(sData, rTargetValue, tid) / targetSize;
						const float targetTemp = finiteTarget ? rTargetValue - targetMean : 0.0f;
						const float targetVar = REDUCE(sData, targetTemp*targetTemp, tid);

						// iteration over the result blocks (block matching part)
						for (unsigned int n = 1; n < blocksRange * 8 /*2*4*/; n += stepSize) {
								for (unsigned int m = 1; m < blocksRange * 8 /*2*4*/; m += stepSize) {
										for (unsigned int l = 1; l < blocksRange * 8 /*2*4*/; l += stepSize) {

												const unsigned int sIdxIn = (idz + n) * numBlocks * 4 * numBlocks * 4 + (idy + m) * numBlocks * 4 + idx + l;

												const float rResultValue = sResultValues[sIdxIn];
												const bool overlap = isfinite(rResultValue) && finiteTarget;
												const unsigned int bSize = REDUCE(sData, overlap ? 1.0f : 0.0f, tid);

												if (bSize > 32){

														float newTargetTemp = targetTemp;
														float newTargetvar = targetVar;
														if (bSize != targetSize){

																const float newTargetValue = overlap ? rTargetValue : 0.0f;
																const float newargetMean = REDUCE(sData, newTargetValue, tid) / bSize;
																newTargetTemp = overlap ? newTargetValue - newargetMean : 0.0f;
																newTargetvar = REDUCE(sData, newTargetTemp*newTargetTemp, tid);
														}

														const float rChecked = overlap ? rResultValue : 0.0f;
														const float resultMean = REDUCE(sData, rChecked, tid) / bSize;
														const float resultTemp = overlap ? rResultValue - resultMean : 0.0f;
														const float resultVar = REDUCE(sData, resultTemp*resultTemp, tid);

														const float sumTargetResult = REDUCE(sData, (newTargetTemp)*(resultTemp), tid);
														const float localCC = fabs((sumTargetResult) / sqrt(newTargetvar*resultVar));

														if (tid == 0 && localCC > bestCC) {
																bestCC = localCC;
																bestDisplacement[0] = l - 4.0f;
																bestDisplacement[1] = m - 4.0f;
																bestDisplacement[2] = n - 4.0f;
														}
												}
										}
								}
						}
				}

				if (tid == 0 /*&& isfinite(bestDisplacement[0])*/) {
						const unsigned int posIdx = 3 * currentBlockIndex;

						referencePosition = start_referencePosition + posIdx;
						warpedPosition = start_warpedPosition + posIdx;

						const float referencePosition_temp[3] = { (float)xBaseImage, (float)yBaseImage, (float)zBaseImage };

						bestDisplacement[0] += referencePosition_temp[0];
						bestDisplacement[1] += referencePosition_temp[1];
						bestDisplacement[2] += referencePosition_temp[2];

						reg_mat44_mul_cl(referenceMatrix_xyz, referencePosition_temp, referencePosition);
						reg_mat44_mul_cl(referenceMatrix_xyz, bestDisplacement, warpedPosition);
						if (isfinite(bestDisplacement[0])) {
								atomic_add(definedBlock, 1);
						}
				}
		}
}
/* *************************************************************** */
/* *************************************************************** */
