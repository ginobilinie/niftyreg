/*
 *  _reg_mind.cpp
 *
 *
 *  Created by Benoit Presles on 01/12/2015.
 *  Copyright (c) 2015, University College London. All rights reserved.
 *  Centre for Medical Image Computing (CMIC)
 *  See the LICENSE.txt file in the nifty_reg root folder
 *
 */

#include "_reg_mind.h"
#include "_reg_ReadWriteImage.h"

/* *************************************************************** */
template <class DTYPE>
void ShiftImage(nifti_image* inputImgPtr,
                nifti_image* shiftedImgPtr,
                int *maskPtr,
                int tx,
                int ty,
                int tz)
{
    DTYPE* inputData = static_cast<DTYPE*> (inputImgPtr->data);
    DTYPE* shiftImageData = static_cast<DTYPE*> (shiftedImgPtr->data);

    int currentIndex;
    int shiftedIndex;

    int x, y, z, old_x, old_y, old_z;

#if defined (_OPENMP)
#pragma omp parallel for default(none) \
   shared(inputData, shiftImageData, shiftedImgPtr, inputImgPtr, \
   maskPtr, tx, ty, tz) \
   private(x, y, z, old_x, old_y, old_z, shiftedIndex, \
   currentIndex)
#endif
    for (z=0;z<shiftedImgPtr->nz;z++) {
       currentIndex = z * shiftedImgPtr->nx * shiftedImgPtr->ny;
        old_z = z-tz;
        for (y=0;y<shiftedImgPtr->ny;y++) {
            old_y = y-ty;
            for (x=0;x<shiftedImgPtr->nx;x++) {
                old_x = x-tx;
                if(old_x>-1 && old_x<inputImgPtr->nx &&
                      old_y>-1 && old_y<inputImgPtr->ny &&
                      old_z>-1 && old_z<inputImgPtr->nz){
                    shiftedIndex = (old_z*inputImgPtr->ny+old_y)*inputImgPtr->nx+old_x;
                    if(maskPtr[shiftedIndex]>-1)
                        shiftImageData[currentIndex]=inputData[shiftedIndex];
//                    else shiftImageData[currentIndex]=std::numeric_limits<DTYPE>::quiet_NaN();
                    else shiftImageData[currentIndex]=0;
                }
//                else shiftImageData[currentIndex]=std::numeric_limits<DTYPE>::quiet_NaN();
                else shiftImageData[currentIndex]=0;
                currentIndex++;
            }
        }
    }
}
/* *************************************************************** */
template<class DTYPE>
void spatialGradient(nifti_image *img,
                     nifti_image *gradImg,
                     int *mask)
{
    size_t voxIndex, voxelNumber = (size_t)img->nx *
                         img->ny * img->nz;

    int dimImg = img->nz > 1 ? 3 : 2;
    int x, y, z;

    DTYPE *imgPtr = static_cast<DTYPE *>(img->data);
    DTYPE *gradPtr = static_cast<DTYPE *>(gradImg->data);
    for(int time=0; time<img->nt; ++time){
        DTYPE *currentImgPtr = &imgPtr[time*voxelNumber];
        DTYPE *gradPtrX = &gradPtr[time*voxelNumber];
        DTYPE *gradPtrY = &gradPtr[(img->nt+time)*voxelNumber];
        DTYPE *gradPtrZ = NULL;
        if(dimImg==3)
            gradPtrZ = &gradPtr[(2*img->nt+time)*voxelNumber];

#if defined (_OPENMP)
#pragma omp parallel for default(none) \
   shared(img, currentImgPtr, mask, \
   gradPtrX, gradPtrY, gradPtrZ) \
   private(x, y, z, voxIndex)
#endif
        for(z=0; z<img->nz; ++z){
           voxIndex=z*img->nx*img->ny;
            for(y=0; y<img->ny; ++y){
                for(x=0; x<img->nx; ++x){
                    if(mask[voxIndex]>-1){
                        if(x<img->nx-1 && x>0)
                            gradPtrX[voxIndex] =  (currentImgPtr[voxIndex+1] - currentImgPtr[voxIndex-1]) / 2.f;
                        else gradPtrX[voxIndex] = 0.f;
                        if(gradPtrX[voxIndex]!=gradPtrX[voxIndex]) gradPtrX[voxIndex]=0.;
                        if(y<img->ny-1 && y>0)
                            gradPtrY[voxIndex] = (currentImgPtr[voxIndex+img->nx] - currentImgPtr[voxIndex-img->nx]) / 2.f;
                        else gradPtrY[voxIndex] = 0.f;
                        if(gradPtrY[voxIndex]!=gradPtrY[voxIndex]) gradPtrY[voxIndex]=0.;
                        if(gradPtrZ!=NULL){
                            if(z<img->nz-1 && z>0)
                                gradPtrZ[voxIndex] = (currentImgPtr[voxIndex+img->nx*img->ny] - currentImgPtr[voxIndex-img->nx*img->ny]) / 2.f;
                            else gradPtrZ[voxIndex] = 0.f;
                            if(gradPtrZ[voxIndex]!=gradPtrZ[voxIndex]) gradPtrZ[voxIndex]=0.;
                        }
                    }
                    ++voxIndex;
                } // x
            } // y
        } // z
    } // t
}
template void spatialGradient<float>(nifti_image *img, nifti_image *gradImg, int *mask);
template void spatialGradient<double>(nifti_image *img, nifti_image *gradImg, int *mask);
/* *************************************************************** */
template <class DTYPE>
void GetMINDImageDesciptor_core(nifti_image* inputImgPtr,
                                nifti_image* MINDImgPtr,
                                int *maskPtr)
{
    const size_t voxNumber = (size_t)inputImgPtr->nx *
                             inputImgPtr->ny * inputImgPtr->nz;
    size_t voxIndex;

    // Create a pointer to the descriptor image
    DTYPE* MINDImgDataPtr = static_cast<DTYPE *>(MINDImgPtr->data);

    // Allocate an image to store the mean image
    nifti_image *mean_img = nifti_copy_nim_info(inputImgPtr);
    mean_img->data=(void *)calloc(inputImgPtr->nvox,inputImgPtr->nbyper);
    DTYPE* meanImgDataPtr = static_cast<DTYPE *>(mean_img->data);

    // Allocate an image to store the warped image
    nifti_image *warpedImage = nifti_copy_nim_info(inputImgPtr);
    warpedImage->data = (void *)malloc(warpedImage->nvox*warpedImage->nbyper);

    // Allocation of the difference image
    nifti_image *diff_image = nifti_copy_nim_info(inputImgPtr);
    diff_image->data = (void *) malloc(diff_image->nvox*diff_image->nbyper);

    // Define the sigma for the convolution
    float sigma = -0.5;// negative value denotes voxel width

    //2D version
    int samplingNbr = (inputImgPtr->nz > 1) ? 6 : 4;
    int RSampling3D_x[6] = {-1, 1,  0, 0,  0, 0};
    int RSampling3D_y[6] = {0,  0, -1, 1,  0, 0};
    int RSampling3D_z[6] = {0,  0,  0, 0, -1, 1};

    for(int i=0;i<samplingNbr;i++) {
        ShiftImage<DTYPE>(inputImgPtr, warpedImage, maskPtr,
                          RSampling3D_x[i], RSampling3D_y[i], RSampling3D_z[i]);
        reg_tools_substractImageToImage(inputImgPtr, warpedImage, diff_image);
        reg_tools_multiplyImageToImage(diff_image, diff_image, diff_image);
        reg_tools_kernelConvolution(diff_image, &sigma, 0, maskPtr);
        reg_tools_addImageToImage(mean_img, diff_image, mean_img);

        // Store the current descriptor
        unsigned int index = i * diff_image->nvox;
        memcpy(&MINDImgDataPtr[index], diff_image->data,
               diff_image->nbyper * diff_image->nvox);
    }
    // Compute the mean over the number of sample
    reg_tools_divideValueToImage(mean_img, mean_img, samplingNbr);

    // Compute the MIND desccriptor
    int mindIndex;
    DTYPE meanValue, max_desc, descValue;
#if defined (_OPENMP)
#pragma omp parallel for default(none) \
   shared(samplingNbr, maskPtr, meanImgDataPtr, \
   MINDImgDataPtr) \
   private(voxIndex, meanValue, max_desc, descValue, mindIndex)
#endif
    for(voxIndex=0;voxIndex<voxNumber;voxIndex++) {

        if(maskPtr[voxIndex]>-1){
            // Get the mean value for the current voxel
            meanValue = meanImgDataPtr[voxIndex];
            if(meanValue == 0) {
                meanValue = std::numeric_limits<DTYPE>::epsilon();
            }
            max_desc = 0;
            mindIndex=voxIndex;
            for(int t=0;t<samplingNbr;t++) {
                descValue = (DTYPE)exp(-MINDImgDataPtr[mindIndex]/meanValue);
                MINDImgDataPtr[mindIndex] = descValue;
                max_desc = std::max(max_desc, descValue);
                mindIndex+=voxNumber;
            }

            mindIndex=voxIndex;
            for(int t=0;t<samplingNbr;t++) {
                descValue = MINDImgDataPtr[mindIndex];
                MINDImgDataPtr[mindIndex] = descValue/max_desc;
                mindIndex+=voxNumber;
            }
        } // mask
    } // voxIndex
    // Mr Propre
    nifti_image_free(diff_image);
    nifti_image_free(warpedImage);
    nifti_image_free(mean_img);
}
/* *************************************************************** */
void GetMINDImageDesciptor(nifti_image* inputImgPtr,
                           nifti_image* MINDImgPtr,
                           int *maskPtr) {
#ifndef NDEBUG
    reg_print_fct_debug("GetMINDImageDesciptor()");
#endif
    if(inputImgPtr->datatype != MINDImgPtr->datatype) {
        reg_print_fct_error("reg_mind -- GetMINDImageDesciptor");
        reg_print_msg_error("The input image and the MIND image must have the same datatype !");
        reg_exit();
    }

    switch (inputImgPtr->datatype)
    {
    case NIFTI_TYPE_FLOAT32:
        GetMINDImageDesciptor_core<float>(inputImgPtr, MINDImgPtr, maskPtr);
        break;
    case NIFTI_TYPE_FLOAT64:
        GetMINDImageDesciptor_core<double>(inputImgPtr, MINDImgPtr, maskPtr);
        break;
    default:
        reg_print_fct_error("GetMINDImageDesciptor");
        reg_print_msg_error("Input image datatype not supported");
        reg_exit();
        break;
    }
}
/* *************************************************************** */
template <class DTYPE>
void GetMINDSSCImageDesciptor_core(nifti_image* inputImgPtr,
                                nifti_image* MINDSSCImgPtr,
                                int *maskPtr)
{
    const size_t voxNumber = (size_t)inputImgPtr->nx *
                             inputImgPtr->ny * inputImgPtr->nz;
    size_t voxIndex;

    // Create a pointer to the descriptor image
    DTYPE* MINDSSCImgDataPtr = static_cast<DTYPE *>(MINDSSCImgPtr->data);

    // Allocate an image to store the mean image
    nifti_image *mean_img = nifti_copy_nim_info(inputImgPtr);
    mean_img->data=(void *)calloc(inputImgPtr->nvox,inputImgPtr->nbyper);
    DTYPE* meanImgDataPtr = static_cast<DTYPE *>(mean_img->data);

    // Allocate an image to store the warped image
    nifti_image *warpedImage = nifti_copy_nim_info(inputImgPtr);
    warpedImage->data = (void *)malloc(warpedImage->nvox*warpedImage->nbyper);

    // Define the sigma for the convolution
    float sigma = -0.5;// negative value denotes voxel width

    //2D version
    int samplingNbr = (inputImgPtr->nz > 1) ? 6 : 2;
    int lengthDescriptor = (inputImgPtr->nz > 1) ? 12 : 4;

    // Allocation of the difference image
    //std::vector<nifti_image *> vectNiftiImage;
    //for(int i=0;i<samplingNbr;i++) {
    nifti_image *diff_image = nifti_copy_nim_info(inputImgPtr);
    diff_image->data = (void *) malloc(diff_image->nvox*diff_image->nbyper);
    int *mask_diff_image = (int *)calloc(diff_image->nvox, sizeof(int));

    nifti_image *diff_imageShifted = nifti_copy_nim_info(inputImgPtr);
    diff_imageShifted->data = (void *) malloc(diff_imageShifted->nvox*diff_imageShifted->nbyper);

    int RSampling3D_x[6] = {+1,+1,-1,+0,+1,+0};
    int RSampling3D_y[6] = {+1,-1,+0,-1,+0,+1};
    int RSampling3D_z[6] = {+0,+0,+1,+1,+1,+1};

    int tx[12]={-1,+0,-1,+0,+0,+1,+0,+0,+0,-1,+0,+0};
    int ty[12]={+0,-1,+0,+1,+0,+0,+0,+1,+0,+0,+0,-1};
    int tz[12]={+0,+0,+0,+0,-1,+0,-1,+0,-1,+0,-1,+0};
    int compteurId = 0;

    for(int i=0;i<samplingNbr;i++) {
        ShiftImage<DTYPE>(inputImgPtr, warpedImage, maskPtr,
                          RSampling3D_x[i], RSampling3D_y[i], RSampling3D_z[i]);
        reg_tools_substractImageToImage(inputImgPtr, warpedImage, diff_image);
        reg_tools_multiplyImageToImage(diff_image, diff_image, diff_image);
        reg_tools_kernelConvolution(diff_image, &sigma, 0, maskPtr);

        for(int j=0;j<2;j++){

            ShiftImage<DTYPE>(diff_image, diff_imageShifted, mask_diff_image,
                              tx[compteurId], ty[compteurId], tz[compteurId]);

            reg_tools_addImageToImage(mean_img, diff_imageShifted, mean_img);
            // Store the current descriptor
            unsigned int index = compteurId * diff_imageShifted->nvox;
            memcpy(&MINDSSCImgDataPtr[index], diff_imageShifted->data,
                   diff_imageShifted->nbyper * diff_imageShifted->nvox);
            compteurId++;
        }
    }
    // Compute the mean over the number of sample
    reg_tools_divideValueToImage(mean_img, mean_img, lengthDescriptor);

    // Compute the MINDSSC desccriptor
    int mindIndex;
    DTYPE meanValue, max_desc, descValue;
#if defined (_OPENMP)
#pragma omp parallel for default(none) \
   shared(lengthDescriptor, samplingNbr, maskPtr, meanImgDataPtr, \
   MINDSSCImgDataPtr) \
   private(voxIndex, meanValue, max_desc, descValue, mindIndex)
#endif
    for(voxIndex=0;voxIndex<voxNumber;voxIndex++) {

        if(maskPtr[voxIndex]>-1){
            // Get the mean value for the current voxel
            meanValue = meanImgDataPtr[voxIndex];
            if(meanValue == 0) {
                meanValue = std::numeric_limits<DTYPE>::epsilon();
            }
            max_desc = 0;
            mindIndex=voxIndex;
            for(int t=0;t<lengthDescriptor;t++) {
                descValue = (DTYPE)exp(-MINDSSCImgDataPtr[mindIndex]/meanValue);
                MINDSSCImgDataPtr[mindIndex] = descValue;
                max_desc = std::max(max_desc, descValue);
                mindIndex+=voxNumber;
            }

            mindIndex=voxIndex;
            for(int t=0;t<lengthDescriptor;t++) {
                descValue = MINDSSCImgDataPtr[mindIndex];
                MINDSSCImgDataPtr[mindIndex] = descValue/max_desc;
                mindIndex+=voxNumber;
            }
        } // mask
    } // voxIndex
    // Mr Propre
    nifti_image_free(diff_imageShifted);
    free(mask_diff_image);
    nifti_image_free(diff_image);
    nifti_image_free(warpedImage);
    nifti_image_free(mean_img);
}
/* *************************************************************** */
void GetMINDSSCImageDesciptor(nifti_image* inputImgPtr,
                           nifti_image* MINDSSCImgPtr,
                           int *maskPtr) {
#ifndef NDEBUG
    reg_print_fct_debug("GetMINDSSCImageDesciptor()");
#endif
    if(inputImgPtr->datatype != MINDSSCImgPtr->datatype) {
        reg_print_fct_error("reg_mindssc -- GetMINDSSCImageDesciptor");
        reg_print_msg_error("The input image and the MINDSSC image must have the same datatype !");
        reg_exit();
    }

    switch (inputImgPtr->datatype)
    {
    case NIFTI_TYPE_FLOAT32:
        GetMINDSSCImageDesciptor_core<float>(inputImgPtr, MINDSSCImgPtr, maskPtr);
        break;
    case NIFTI_TYPE_FLOAT64:
        GetMINDSSCImageDesciptor_core<double>(inputImgPtr, MINDSSCImgPtr, maskPtr);
        break;
    default:
        reg_print_fct_error("GetMINDSSCImageDesciptor");
        reg_print_msg_error("Input image datatype not supported");
        reg_exit();
        break;
    }
}
/* *************************************************************** */
reg_mind::reg_mind()
    : reg_ssd()
{
    memset(this->activeTimePointDescriptor,0,255*sizeof(bool) );
    this->referenceImageDescriptor=NULL;
    this->floatingImageDescriptor=NULL;
    this->warpedFloatingImageDescriptor=NULL;
    this->warpedReferenceImageDescriptor=NULL;
    this->warpedFloatingImageDescriptorGradient=NULL;
    this->warpedReferenceImageDescriptorGradient=NULL;
#ifndef NDEBUG
    reg_print_msg_debug("reg_mind constructor called");
#endif
}
/* *************************************************************** */
reg_mind::~reg_mind() {
    if(this->referenceImageDescriptor != NULL)
        nifti_image_free(this->referenceImageDescriptor);
    this->referenceImageDescriptor = NULL;

    if(this->warpedFloatingImageDescriptor != NULL)
        nifti_image_free(this->warpedFloatingImageDescriptor);
    this->warpedFloatingImageDescriptor = NULL;

    if(this->warpedFloatingImageDescriptorGradient != NULL)
        nifti_image_free(this->warpedFloatingImageDescriptorGradient);
    this->warpedFloatingImageDescriptorGradient = NULL;

    if(this->floatingImageDescriptor != NULL)
        nifti_image_free(this->floatingImageDescriptor);
    this->floatingImageDescriptor = NULL;

    if(this->warpedReferenceImageDescriptor != NULL)
        nifti_image_free(this->warpedReferenceImageDescriptor);
    this->warpedReferenceImageDescriptor = NULL;

    if(this->warpedReferenceImageDescriptorGradient != NULL)
        nifti_image_free(this->warpedReferenceImageDescriptorGradient);
    this->warpedReferenceImageDescriptorGradient = NULL;
}
/* *************************************************************** */
void reg_mind::InitialiseMeasure(nifti_image *refImgPtr,
                                 nifti_image *floImgPtr,
                                 int *maskRefPtr,
                                 nifti_image *warFloImgPtr,
                                 nifti_image *warFloGraPtr,
                                 nifti_image *forVoxBasedGraPtr,
                                 int *maskFloPtr,
                                 nifti_image *warRefImgPtr,
                                 nifti_image *warRefGraPtr,
                                 nifti_image *bckVoxBasedGraPtr)
{
    // Set the pointers using the parent class function
    reg_ssd::InitialiseMeasure(refImgPtr,
                               floImgPtr,
                               maskRefPtr,
                               warFloImgPtr,
                               warFloGraPtr,
                               forVoxBasedGraPtr,
                               maskFloPtr,
                               warRefImgPtr,
                               warRefGraPtr,
                               bckVoxBasedGraPtr);

    if(this->referenceImagePointer->nt>1 || this->warpedFloatingImagePointer->nt>1){
        reg_print_msg_error("reg_mind does not support multiple time point image");
        reg_exit();
    }

    // Initialise the reference descriptor
    int dim=this->referenceImagePointer->nz>1?3:2;
    this->referenceImageDescriptor = nifti_copy_nim_info(this->referenceImagePointer);
    this->referenceImageDescriptor->dim[0]=this->referenceImageDescriptor->ndim=4;
    this->referenceImageDescriptor->dim[4]=this->referenceImageDescriptor->nt=dim*2;
    this->referenceImageDescriptor->nvox = (size_t)this->referenceImageDescriptor->nx*
                                           this->referenceImageDescriptor->ny*
                                           this->referenceImageDescriptor->nz*
                                           this->referenceImageDescriptor->nt;
    this->referenceImageDescriptor->data=(void *)malloc(this->referenceImageDescriptor->nvox*
                                                        this->referenceImageDescriptor->nbyper);
    // Initialise the warped floating descriptor
    this->warpedFloatingImageDescriptor = nifti_copy_nim_info(this->referenceImagePointer);
    this->warpedFloatingImageDescriptor->dim[0]=this->warpedFloatingImageDescriptor->ndim=4;
    this->warpedFloatingImageDescriptor->dim[4]=this->warpedFloatingImageDescriptor->nt=dim*2;
    this->warpedFloatingImageDescriptor->nvox = (size_t)this->warpedFloatingImageDescriptor->nx*
                                                this->warpedFloatingImageDescriptor->ny*
                                                this->warpedFloatingImageDescriptor->nz*
                                                this->warpedFloatingImageDescriptor->nt;
    this->warpedFloatingImageDescriptor->data=(void *)malloc(this->warpedFloatingImageDescriptor->nvox*
                                                             this->warpedFloatingImageDescriptor->nbyper);
    // Initialise the warped gradient descriptor
    this->warpedFloatingImageDescriptorGradient = nifti_copy_nim_info(this->referenceImagePointer);
    this->warpedFloatingImageDescriptorGradient->dim[0]=this->warpedFloatingImageDescriptorGradient->ndim=5;
    this->warpedFloatingImageDescriptorGradient->dim[4]=this->warpedFloatingImageDescriptorGradient->nt=dim*2;
    this->warpedFloatingImageDescriptorGradient->dim[5]=this->warpedFloatingImageDescriptorGradient->nu=dim;
    this->warpedFloatingImageDescriptorGradient->nvox = (size_t)this->warpedFloatingImageDescriptorGradient->nx*
                                                        this->warpedFloatingImageDescriptorGradient->ny*
                                                        this->warpedFloatingImageDescriptorGradient->nz*
                                                        this->warpedFloatingImageDescriptorGradient->nt*
                                                        this->warpedFloatingImageDescriptorGradient->nu;
    this->warpedFloatingImageDescriptorGradient->data=(void *)malloc(this->warpedFloatingImageDescriptorGradient->nvox*
                                                                     this->warpedFloatingImageDescriptorGradient->nbyper);

    if(this->isSymmetric) {
        if(this->floatingImagePointer->nt>1 || this->warpedReferenceImagePointer->nt>1){
            reg_print_msg_error("reg_mind does not support multiple time point image");
            reg_exit();
        }
        // Initialise the floating descriptor
        int dim=this->floatingImagePointer->nz>1?3:2;
        this->floatingImageDescriptor = nifti_copy_nim_info(this->floatingImagePointer);
        this->floatingImageDescriptor->dim[0]=this->floatingImageDescriptor->ndim=4;
        this->floatingImageDescriptor->dim[4]=this->floatingImageDescriptor->nt=dim*2;
        this->floatingImageDescriptor->nvox = (size_t)this->floatingImageDescriptor->nx*
                                              this->floatingImageDescriptor->ny*
                                              this->floatingImageDescriptor->nz*
                                              this->floatingImageDescriptor->nt;
        this->floatingImageDescriptor->data=(void *)malloc(this->floatingImageDescriptor->nvox*
                                                           this->floatingImageDescriptor->nbyper);
        // Initialise the warped floating descriptor
        this->warpedReferenceImageDescriptor = nifti_copy_nim_info(this->floatingImagePointer);
        this->warpedReferenceImageDescriptor->dim[0]=this->warpedReferenceImageDescriptor->ndim=4;
        this->warpedReferenceImageDescriptor->dim[4]=this->warpedReferenceImageDescriptor->nt=dim*2;
        this->warpedReferenceImageDescriptor->nvox = (size_t)this->warpedReferenceImageDescriptor->nx*
                                                     this->warpedReferenceImageDescriptor->ny*
                                                     this->warpedReferenceImageDescriptor->nz*
                                                     this->warpedReferenceImageDescriptor->nt;
        this->warpedReferenceImageDescriptor->data=(void *)malloc(this->warpedReferenceImageDescriptor->nvox*
                                                                  this->warpedReferenceImageDescriptor->nbyper);
        // Initialise the warped gradient descriptor
        this->warpedReferenceImageDescriptorGradient = nifti_copy_nim_info(this->floatingImagePointer);
        this->warpedReferenceImageDescriptorGradient->dim[0]=this->warpedReferenceImageDescriptorGradient->ndim=5;
        this->warpedReferenceImageDescriptorGradient->dim[4]=this->warpedReferenceImageDescriptorGradient->nt=dim*2;
        this->warpedReferenceImageDescriptorGradient->dim[5]=this->warpedReferenceImageDescriptorGradient->nu=dim;
        this->warpedReferenceImageDescriptorGradient->nvox = (size_t)this->warpedReferenceImageDescriptorGradient->nx*
                                                             this->warpedReferenceImageDescriptorGradient->ny*
                                                             this->warpedReferenceImageDescriptorGradient->nz*
                                                             this->warpedReferenceImageDescriptorGradient->nt*
                                                             this->warpedReferenceImageDescriptorGradient->nu;
        this->warpedReferenceImageDescriptorGradient->data=(void *)malloc(this->warpedReferenceImageDescriptorGradient->nvox*
                                                                         this->warpedReferenceImageDescriptorGradient->nbyper);
    }

    for(int i=0;i<referenceImageDescriptor->nt;++i) {
        this->activeTimePointDescriptor[i]=true;
    }

#ifndef NDEBUG
    char text[255];
    reg_print_msg_debug("reg_mind::InitialiseMeasure().");
    sprintf(text, "Active time point:");
    for(int i=0; i<this->referenceImageDescriptor->nt; ++i)
        if(this->activeTimePointDescriptor[i])
            sprintf(text, "%s %i", text, i);
    reg_print_msg_debug(text);
#endif
}
/* *************************************************************** */
double reg_mind::GetSimilarityMeasureValue()
{
    size_t voxelNumber = (size_t)referenceImagePointer->nx *
                         referenceImagePointer->ny * referenceImagePointer->nz;
    int *combinedMask = (int *)malloc(voxelNumber*sizeof(int));
    memcpy(combinedMask, this->referenceMaskPointer, voxelNumber*sizeof(int));
    reg_tools_removeNanFromMask(this->referenceImagePointer, combinedMask);
    reg_tools_removeNanFromMask(this->warpedFloatingImagePointer, combinedMask);

    GetMINDImageDesciptor(this->referenceImagePointer,
                          this->referenceImageDescriptor,
                          combinedMask);
    GetMINDImageDesciptor(this->warpedFloatingImagePointer,
                          this->warpedFloatingImageDescriptor,
                          combinedMask);

    double MINDValue;
    switch(this->referenceImageDescriptor->datatype)
    {
    case NIFTI_TYPE_FLOAT32:
        MINDValue = reg_getSSDValue<float>
                   (this->referenceImageDescriptor,
                    this->warpedFloatingImageDescriptor,
                    this->activeTimePointDescriptor,
                    NULL, // HERE TODO this->forwardJacDetImagePointer,
                    combinedMask,
                    this->currentValue
                    );
        break;
    case NIFTI_TYPE_FLOAT64:
        MINDValue = reg_getSSDValue<double>
                   (this->referenceImageDescriptor,
                    this->warpedFloatingImageDescriptor,
                    this->activeTimePointDescriptor,
                    NULL, // HERE TODO this->forwardJacDetImagePointer,
                    combinedMask,
                    this->currentValue
                    );
        break;
    default:
        reg_print_fct_error("reg_mind::GetSimilarityMeasureValue");
        reg_print_msg_error("Warped pixel type unsupported");
        reg_exit();
    }
    free(combinedMask);

    // Backward computation
    if(this->isSymmetric)
    {
        voxelNumber = (size_t)floatingImagePointer->nx *
                      floatingImagePointer->ny * floatingImagePointer->nz;
        combinedMask = (int *)malloc(voxelNumber*sizeof(int));
        memcpy(combinedMask, this->referenceMaskPointer, voxelNumber*sizeof(int));
        reg_tools_removeNanFromMask(this->referenceImagePointer, combinedMask);
        reg_tools_removeNanFromMask(this->warpedFloatingImagePointer, combinedMask);
        GetMINDImageDesciptor(this->floatingImagePointer,
                              this->floatingImageDescriptor,
                              combinedMask);
        GetMINDImageDesciptor(this->warpedReferenceImagePointer,
                              this->warpedReferenceImageDescriptor,
                              combinedMask);

        switch(this->floatingImageDescriptor->datatype)
        {
        case NIFTI_TYPE_FLOAT32:
            MINDValue += reg_getSSDValue<float>
                        (this->floatingImageDescriptor,
                         this->warpedReferenceImageDescriptor,
                         this->activeTimePointDescriptor,
                         NULL, // HERE TODO this->backwardJacDetImagePointer,
                         combinedMask,
                         this->currentValue
                         );
            break;
        case NIFTI_TYPE_FLOAT64:
            MINDValue += reg_getSSDValue<double>
                        (this->floatingImageDescriptor,
                         this->warpedReferenceImageDescriptor,
                         this->activeTimePointDescriptor,
                         NULL, // HERE TODO this->backwardJacDetImagePointer,
                         combinedMask,
                         this->currentValue
                         );
            break;
        default:
            reg_print_fct_error("reg_mind::GetSimilarityMeasureValue");
            reg_print_msg_error("Warped pixel type unsupported");
            reg_exit();
        }
        free(combinedMask);
    }
    return MINDValue;// /(double) this->referenceImageDescriptor->nt;
}
/* *************************************************************** */
void reg_mind::GetVoxelBasedSimilarityMeasureGradient()
{
    // Create a combined mask to ignore masked and undefined values
    size_t voxelNumber = (size_t)this->referenceImagePointer->nx *
                         this->referenceImagePointer->ny *
                         this->referenceImagePointer->nz;
    int *combinedMask = (int *)malloc(voxelNumber*sizeof(int));
    memcpy(combinedMask, this->referenceMaskPointer, voxelNumber*sizeof(int));
    reg_tools_removeNanFromMask(this->referenceImagePointer, combinedMask);
    reg_tools_removeNanFromMask(this->warpedFloatingImagePointer, combinedMask);

    // Compute the reference image descriptors
    GetMINDImageDesciptor(this->referenceImagePointer,
                          this->referenceImageDescriptor,
                          combinedMask);
    // Compute the warped floating image descriptors
    GetMINDImageDesciptor(this->warpedFloatingImagePointer,
                          this->warpedFloatingImageDescriptor,
                          combinedMask);

    // Compute the warped image descriptors gradient
    spatialGradient<float>(this->warpedFloatingImageDescriptor,
                           this->warpedFloatingImageDescriptorGradient,
                           combinedMask);

    // Compute the gradient of the ssd for the forward transformation
    switch(referenceImageDescriptor->datatype)
    {
    case NIFTI_TYPE_FLOAT32:
        reg_getVoxelBasedSSDGradient<float>
                (this->referenceImageDescriptor,
                 this->warpedFloatingImageDescriptor,
                 this->activeTimePointDescriptor,
                 this->warpedFloatingImageDescriptorGradient,
                 this->forwardVoxelBasedGradientImagePointer,
                 NULL, // no Jacobian required here,
                 combinedMask
                 );
        break;
    case NIFTI_TYPE_FLOAT64:
        reg_getVoxelBasedSSDGradient<double>
                (this->referenceImageDescriptor,
                 this->warpedFloatingImageDescriptor,
                 this->activeTimePointDescriptor,
                 this->warpedFloatingImageDescriptorGradient,
                 this->forwardVoxelBasedGradientImagePointer,
                 NULL, // no Jacobian required here,
                 combinedMask
                 );
        break;
    default:
        reg_print_fct_error("reg_mind::GetVoxelBasedSimilarityMeasureGradient");
        reg_print_msg_error("Unsupported datatype");
        reg_exit();
    }
    free(combinedMask);
    // Compute the gradient of the ssd for the backward transformation
    if(this->isSymmetric)
    {
        voxelNumber = (size_t)floatingImagePointer->nx *
                      floatingImagePointer->ny * floatingImagePointer->nz;
        combinedMask = (int *)malloc(voxelNumber*sizeof(int));
        memcpy(combinedMask, this->referenceMaskPointer, voxelNumber*sizeof(int));
        reg_tools_removeNanFromMask(this->referenceImagePointer, combinedMask);
        reg_tools_removeNanFromMask(this->warpedFloatingImagePointer, combinedMask);
        GetMINDImageDesciptor(this->floatingImagePointer,
                              this->floatingImageDescriptor,
                              combinedMask);
        GetMINDImageDesciptor(this->warpedReferenceImagePointer,
                              this->warpedReferenceImageDescriptor,
                              combinedMask);

        spatialGradient<float>(this->warpedReferenceImageDescriptor,
                                    this->warpedReferenceImageDescriptorGradient,
                                    combinedMask);

        // Compute the gradient of the nmi for the backward transformation
        switch(floatingImagePointer->datatype)
        {
        case NIFTI_TYPE_FLOAT32:
            reg_getVoxelBasedSSDGradient<float>
                    (this->floatingImageDescriptor,
                     this->warpedReferenceImageDescriptor,
                     this->activeTimePointDescriptor,
                     this->warpedReferenceImageDescriptorGradient,
                     this->backwardVoxelBasedGradientImagePointer,
                     NULL, // no Jacobian required here,
                     combinedMask
                     );
            break;
        case NIFTI_TYPE_FLOAT64:
            reg_getVoxelBasedSSDGradient<double>
                    (this->floatingImageDescriptor,
                     this->warpedReferenceImageDescriptor,
                     this->activeTimePointDescriptor,
                     this->warpedReferenceImageDescriptorGradient,
                     this->backwardVoxelBasedGradientImagePointer,
                     NULL, // no Jacobian required here,
                     combinedMask
                     );
            break;
        default:
            reg_print_fct_error("reg_mind::GetVoxelBasedSimilarityMeasureGradient");
            reg_print_msg_error("Unsupported datatype");
            reg_exit();
        }
        free(combinedMask);
    }
}
/* *************************************************************** */
/* *************************************************************** */
reg_mindssc::reg_mindssc()
    : reg_mind()
{
#ifndef NDEBUG
    reg_print_msg_debug("reg_mindssc constructor called");
#endif
}
/* *************************************************************** */
reg_mindssc::~reg_mindssc()
{
#ifndef NDEBUG
    reg_print_msg_debug("reg_mindssc desctructor called");
#endif
}
/* *************************************************************** */
void reg_mindssc::InitialiseMeasure(nifti_image *refImgPtr,
                                 nifti_image *floImgPtr,
                                 int *maskRefPtr,
                                 nifti_image *warFloImgPtr,
                                 nifti_image *warFloGraPtr,
                                 nifti_image *forVoxBasedGraPtr,
                                 int *maskFloPtr,
                                 nifti_image *warRefImgPtr,
                                 nifti_image *warRefGraPtr,
                                 nifti_image *bckVoxBasedGraPtr)
{
    // Set the pointers using the parent class function
    reg_ssd::InitialiseMeasure(refImgPtr,
                               floImgPtr,
                               maskRefPtr,
                               warFloImgPtr,
                               warFloGraPtr,
                               forVoxBasedGraPtr,
                               maskFloPtr,
                               warRefImgPtr,
                               warRefGraPtr,
                               bckVoxBasedGraPtr);

    if(this->referenceImagePointer->nt>1 || this->warpedFloatingImagePointer->nt>1){
        reg_print_msg_error("reg_mindssc does not support multiple time point image");
        reg_exit();
    }

    // Initialise the reference descriptor
    int dim=this->referenceImagePointer->nz>1?3:2;
    int dimt=this->referenceImagePointer->nz>1?12:4;
    this->referenceImageDescriptor = nifti_copy_nim_info(this->referenceImagePointer);
    this->referenceImageDescriptor->dim[0]=this->referenceImageDescriptor->ndim=4;
    this->referenceImageDescriptor->dim[4]=this->referenceImageDescriptor->nt=dimt;
    this->referenceImageDescriptor->nvox = (size_t)this->referenceImageDescriptor->nx*
                                           this->referenceImageDescriptor->ny*
                                           this->referenceImageDescriptor->nz*
                                           this->referenceImageDescriptor->nt;
    this->referenceImageDescriptor->data=(void *)malloc(this->referenceImageDescriptor->nvox*
                                                        this->referenceImageDescriptor->nbyper);
    // Initialise the warped floating descriptor
    this->warpedFloatingImageDescriptor = nifti_copy_nim_info(this->referenceImagePointer);
    this->warpedFloatingImageDescriptor->dim[0]=this->warpedFloatingImageDescriptor->ndim=4;
    this->warpedFloatingImageDescriptor->dim[4]=this->warpedFloatingImageDescriptor->nt=dimt;
    this->warpedFloatingImageDescriptor->nvox = (size_t)this->warpedFloatingImageDescriptor->nx*
                                                this->warpedFloatingImageDescriptor->ny*
                                                this->warpedFloatingImageDescriptor->nz*
                                                this->warpedFloatingImageDescriptor->nt;
    this->warpedFloatingImageDescriptor->data=(void *)malloc(this->warpedFloatingImageDescriptor->nvox*
                                                             this->warpedFloatingImageDescriptor->nbyper);
    // Initialise the warped gradient descriptor
    this->warpedFloatingImageDescriptorGradient = nifti_copy_nim_info(this->referenceImagePointer);
    this->warpedFloatingImageDescriptorGradient->dim[0]=this->warpedFloatingImageDescriptorGradient->ndim=5;
    this->warpedFloatingImageDescriptorGradient->dim[4]=this->warpedFloatingImageDescriptorGradient->nt=dimt;
    this->warpedFloatingImageDescriptorGradient->dim[5]=this->warpedFloatingImageDescriptorGradient->nu=dim;
    this->warpedFloatingImageDescriptorGradient->nvox = (size_t)this->warpedFloatingImageDescriptorGradient->nx*
                                                        this->warpedFloatingImageDescriptorGradient->ny*
                                                        this->warpedFloatingImageDescriptorGradient->nz*
                                                        this->warpedFloatingImageDescriptorGradient->nt*
                                                        this->warpedFloatingImageDescriptorGradient->nu;
    this->warpedFloatingImageDescriptorGradient->data=(void *)malloc(this->warpedFloatingImageDescriptorGradient->nvox*
                                                                     this->warpedFloatingImageDescriptorGradient->nbyper);

    if(this->isSymmetric) {
        if(this->floatingImagePointer->nt>1 || this->warpedReferenceImagePointer->nt>1){
            reg_print_msg_error("reg_mindssc does not support multiple time point image");
            reg_exit();
        }
        // Initialise the floating descriptor
        int dim=this->floatingImagePointer->nz>1?3:2;
        int dimt=this->referenceImagePointer->nz>1?12:4;
        this->floatingImageDescriptor = nifti_copy_nim_info(this->floatingImagePointer);
        this->floatingImageDescriptor->dim[0]=this->floatingImageDescriptor->ndim=4;
        this->floatingImageDescriptor->dim[4]=this->floatingImageDescriptor->nt=dimt;
        this->floatingImageDescriptor->nvox = (size_t)this->floatingImageDescriptor->nx*
                                              this->floatingImageDescriptor->ny*
                                              this->floatingImageDescriptor->nz*
                                              this->floatingImageDescriptor->nt;
        this->floatingImageDescriptor->data=(void *)malloc(this->floatingImageDescriptor->nvox*
                                                           this->floatingImageDescriptor->nbyper);
        // Initialise the warped floating descriptor
        this->warpedReferenceImageDescriptor = nifti_copy_nim_info(this->floatingImagePointer);
        this->warpedReferenceImageDescriptor->dim[0]=this->warpedReferenceImageDescriptor->ndim=4;
        this->warpedReferenceImageDescriptor->dim[4]=this->warpedReferenceImageDescriptor->nt=dimt;
        this->warpedReferenceImageDescriptor->nvox = (size_t)this->warpedReferenceImageDescriptor->nx*
                                                     this->warpedReferenceImageDescriptor->ny*
                                                     this->warpedReferenceImageDescriptor->nz*
                                                     this->warpedReferenceImageDescriptor->nt;
        this->warpedReferenceImageDescriptor->data=(void *)malloc(this->warpedReferenceImageDescriptor->nvox*
                                                                  this->warpedReferenceImageDescriptor->nbyper);
        // Initialise the warped gradient descriptor
        this->warpedReferenceImageDescriptorGradient = nifti_copy_nim_info(this->floatingImagePointer);
        this->warpedReferenceImageDescriptorGradient->dim[0]=this->warpedReferenceImageDescriptorGradient->ndim=5;
        this->warpedReferenceImageDescriptorGradient->dim[4]=this->warpedReferenceImageDescriptorGradient->nt=dimt;
        this->warpedReferenceImageDescriptorGradient->dim[5]=this->warpedReferenceImageDescriptorGradient->nu=dim;
        this->warpedReferenceImageDescriptorGradient->nvox = (size_t)this->warpedReferenceImageDescriptorGradient->nx*
                                                             this->warpedReferenceImageDescriptorGradient->ny*
                                                             this->warpedReferenceImageDescriptorGradient->nz*
                                                             this->warpedReferenceImageDescriptorGradient->nt*
                                                             this->warpedReferenceImageDescriptorGradient->nu;
        this->warpedReferenceImageDescriptorGradient->data=(void *)malloc(this->warpedReferenceImageDescriptorGradient->nvox*
                                                                         this->warpedReferenceImageDescriptorGradient->nbyper);
    }

    for(int i=0;i<referenceImageDescriptor->nt;++i) {
        this->activeTimePointDescriptor[i]=true;
    }

#ifndef NDEBUG
    char text[255];
    reg_print_msg_debug("reg_mindssc::InitialiseMeasure().");
    sprintf(text, "Active time point:");
    for(int i=0; i<this->referenceImageDescriptor->nt; ++i)
        if(this->activeTimePointDescriptor[i])
            sprintf(text, "%s %i", text, i);
    reg_print_msg_debug(text);
#endif
}
/* *************************************************************** */
