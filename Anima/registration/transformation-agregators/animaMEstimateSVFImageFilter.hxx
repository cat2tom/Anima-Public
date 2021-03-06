#pragma once
#include "animaMEstimateSVFImageFilter.h"

#include <itkImageRegionIteratorWithIndex.h>
#include <itkImageRegionConstIteratorWithIndex.h>

#include <animaSmoothingRecursiveYvvGaussianImageFilter.h>

namespace anima
{

template <class TScalarType, unsigned int NDegreesOfFreedom, unsigned int NDimensions>
void
MEstimateSVFImageFilter<TScalarType,NDegreesOfFreedom,NDimensions>::
BeforeThreadedGenerateData ()
{
    unsigned int nbInputs = this->GetNumberOfIndexedInputs();
    if (nbInputs != 1)
        itkExceptionMacro("Error: There should be one input...");

    WeightImagePointer tmpWeights = WeightImageType::New();
    typedef itk::ImageRegionIterator <WeightImageType> WeightIteratorType;

    WeightIteratorType weightOriginalIterator(m_WeightImage,this->GetInput()->GetLargestPossibleRegion());

    tmpWeights->SetRegions (this->GetInput()->GetLargestPossibleRegion());
    tmpWeights->SetSpacing (this->GetInput()->GetSpacing());
    tmpWeights->SetOrigin (this->GetInput()->GetOrigin());
    tmpWeights->SetDirection (this->GetInput()->GetDirection());
    tmpWeights->Allocate();
    tmpWeights->FillBuffer(0);

    WeightIteratorType tmpWeightIterator(tmpWeights,tmpWeights->GetLargestPossibleRegion());

    while (!tmpWeightIterator.IsAtEnd())
    {
        if (weightOriginalIterator.Value() <= 0)
        {
            ++weightOriginalIterator;
            ++tmpWeightIterator;
            continue;
        }

        tmpWeightIterator.Set(1.0);
        ++weightOriginalIterator;
        ++tmpWeightIterator;
    }

    typedef anima::SmoothingRecursiveYvvGaussianImageFilter<WeightImageType,WeightImageType> WeightSmootherType;
    typename WeightSmootherType::Pointer weightSmooth = WeightSmootherType::New();

    weightSmooth->SetInput(tmpWeights);
    weightSmooth->SetSigma(m_FluidSigma);
    weightSmooth->SetNumberOfThreads(this->GetNumberOfThreads());

    weightSmooth->Update();

    typedef anima::SmoothingRecursiveYvvGaussianImageFilter<TInputImage,TInputImage> FieldSmootherType;
    typename FieldSmootherType::Pointer fieldSmooth = FieldSmootherType::New();

    fieldSmooth->SetInput(this->GetInput());
    fieldSmooth->SetSigma(m_FluidSigma);
    fieldSmooth->SetNumberOfThreads(this->GetNumberOfThreads());

    fieldSmooth->Update();

    InputImagePointer smoothSVFImage = fieldSmooth->GetOutput();
    smoothSVFImage->DisconnectPipeline();

    WeightImagePointer smoothWeightImage = weightSmooth->GetOutput();

    OutputPixelType curDisp;
    OutputPixelType originDisp;
    double averageDist = 0;
    unsigned int numPairings = 0;
    InputIndexType tmpIndex;

    typedef itk::ImageRegionIterator <WeightImageType> WeightIteratorWithIndexType;
    WeightIteratorWithIndexType tmpWeightIteratorWithIndex(tmpWeights,tmpWeights->GetLargestPossibleRegion());

    while (!tmpWeightIteratorWithIndex.IsAtEnd())
    {
        if (tmpWeightIteratorWithIndex.Value() <= 0)
        {
            ++tmpWeightIteratorWithIndex;
            continue;
        }

        tmpIndex = tmpWeightIteratorWithIndex.GetIndex();
        curDisp = smoothSVFImage->GetPixel(tmpIndex);
        curDisp /= smoothWeightImage->GetPixel(tmpIndex);

        originDisp = this->GetInput()->GetPixel(tmpIndex);

        double dist = 0;
        for (unsigned int i = 0;i < NDegreesOfFreedom;++i)
            dist += (curDisp[i] - originDisp[i]) * (curDisp[i] - originDisp[i]);

        averageDist += dist;

        ++numPairings;
        ++tmpWeightIteratorWithIndex;
    }

    m_AverageResidualValue = averageDist / numPairings;

    // Now compute image of spatial weights
    OutputImageRegionType tmpRegion;
    InputIndexType centerIndex, curIndex;
    InputPointType curPosition, centerPosition;

    for (unsigned int i = 0;i < NDimensions;++i)
    {
        tmpRegion.SetIndex(i,0);
        tmpRegion.SetSize(i,2 * m_NeighborhoodHalfSize + 1);
        centerIndex[i] = m_NeighborhoodHalfSize;
    }

    m_InternalSpatialWeight = WeightImageType::New();

    m_InternalSpatialWeight->Initialize();
    m_InternalSpatialWeight->SetRegions (tmpRegion);
    m_InternalSpatialWeight->SetSpacing (this->GetInput()->GetSpacing());
    m_InternalSpatialWeight->SetOrigin (this->GetInput()->GetOrigin());
    m_InternalSpatialWeight->SetDirection (this->GetInput()->GetDirection());
    m_InternalSpatialWeight->Allocate();

    WeightIteratorWithIndexType spatialWeightItr(m_InternalSpatialWeight,tmpRegion);
    m_InternalSpatialWeight->TransformIndexToPhysicalPoint(centerIndex,centerPosition);

    while (!spatialWeightItr.IsAtEnd())
    {
        curIndex = spatialWeightItr.GetIndex();
        m_InternalSpatialWeight->TransformIndexToPhysicalPoint(curIndex,curPosition);

        double centerDist = 0;
        for (unsigned int i = 0;i < NDimensions;++i)
            centerDist += (centerPosition[i] - curPosition[i]) * (centerPosition[i] - curPosition[i]);

        if (centerDist < m_SqrDistanceBoundary)
            spatialWeightItr.Set(std::exp(- centerDist / (2.0 * m_FluidSigma * m_FluidSigma)));
        else
            spatialWeightItr.Set(0);

        ++spatialWeightItr;
    }
}

template <class TScalarType, unsigned int NDegreesOfFreedom, unsigned int NDimensions>
void
MEstimateSVFImageFilter<TScalarType,NDegreesOfFreedom,NDimensions>::
ThreadedGenerateData (const OutputImageRegionType &outputRegionForThread, itk::ThreadIdType threadId)
{
    typedef itk::ImageRegionIteratorWithIndex <TOutputImage> OutRegionIteratorType;
    typedef itk::ImageRegionConstIteratorWithIndex <WeightImageType> WeightIteratorWithIndexType;
    typedef itk::ImageRegionConstIterator <WeightImageType> WeightIteratorType;

    OutRegionIteratorType outIterator(this->GetOutput(), outputRegionForThread);

    unsigned int numMaxEltsRegion = 1;
    for (unsigned int i = 0;i < NDimensions;++i)
        numMaxEltsRegion *= (2 * m_NeighborhoodHalfSize + 1);

    std::vector <double> weightsVector(numMaxEltsRegion);
    std::vector <double> deltaVector(numMaxEltsRegion);
    std::vector <InputPixelType> logTrsfsVector(numMaxEltsRegion);

    InputIndexType curIndex, centerIndex, spatialIndex;
    std::vector <int> diffSpatialIndex(NDimensions,0);

    OutputImageRegionType tmpRegion;
    OutputImageRegionType largestRegion = this->GetOutput()->GetLargestPossibleRegion();
    std::vector <unsigned int> largestBound(NDimensions,0);
    for (unsigned int i = 0;i < NDimensions;++i)
        largestBound[i] = largestRegion.GetIndex()[i] + largestRegion.GetSize()[i] - 1;

    OutputPixelType outValue, outValueOld;
    WeightIteratorType damWeightsItr;
    if (m_BlockDamWeights)
        damWeightsItr = WeightIteratorType(m_BlockDamWeights,outputRegionForThread);

    while (!outIterator.IsAtEnd())
    {
        outValue.Fill(0);
        if (m_BlockDamWeights)
        {
            if (damWeightsItr.Get() <= 0)
            {
                outIterator.Set(outValue);
                ++outIterator;
                ++damWeightsItr;
                continue;
            }
        }

        centerIndex = outIterator.GetIndex();

        for (unsigned int i = 0;i < NDimensions;++i)
        {
            diffSpatialIndex[i] = m_NeighborhoodHalfSize - centerIndex[i];
            tmpRegion.SetIndex(i,std::max(0,- diffSpatialIndex[i]));
            tmpRegion.SetSize(i,std::min(largestBound[i], (unsigned int)centerIndex[i] + m_NeighborhoodHalfSize) - tmpRegion.GetIndex()[i] + 1);
        }

        WeightIteratorWithIndexType weightIterator(m_WeightImage,tmpRegion);

        double sumAbsoluteWeights = 0;
        unsigned int pos = 0;
        double spatialWeight = 0;

        while (!weightIterator.IsAtEnd())
        {
            if (weightIterator.Value() <= 0)
            {
                ++weightIterator;
                continue;
            }

            curIndex = weightIterator.GetIndex();
            for (unsigned int i = 0;i < NDimensions;++i)
                spatialIndex[i] = curIndex[i] + diffSpatialIndex[i];

            spatialWeight = m_InternalSpatialWeight->GetPixel(spatialIndex);
            if (spatialWeight <= 0)
            {
                ++weightIterator;
                continue;
            }

            logTrsfsVector[pos] = this->GetInput()->GetPixel(curIndex);
            weightsVector[pos] = weightIterator.Value();
            sumAbsoluteWeights += weightsVector[pos];

            weightsVector[pos] *= spatialWeight;

            ++pos;
            ++weightIterator;
        }

        unsigned int numEltsRegion = pos;

        if ((sumAbsoluteWeights <= 0)||(numEltsRegion < 1))
        {
            outIterator.Set(outValue);
            ++outIterator;
            if (m_BlockDamWeights)
                ++damWeightsItr;
            continue;
        }

        bool stopLoop = false;
        unsigned int numIter = 0;
        std::fill(deltaVector.begin(),deltaVector.begin()+numEltsRegion,1.0);

        while (!stopLoop)
        {
            ++numIter;

            outValueOld = outValue;
            outValue.Fill(0);

            double sumWeights = 0;
            for (unsigned int i = 0;i < numEltsRegion;++i)
            {
                double tmpWeight = weightsVector[i] * deltaVector[i];
                outValue += logTrsfsVector[i] * tmpWeight;

                sumWeights += tmpWeight;
            }

            outValue /= sumWeights;

            if (numIter == m_MaxNumIterations)
                stopLoop = true;

            if (!stopLoop)
                stopLoop = checkConvergenceThreshold(outValueOld,outValue);

            if (!stopLoop)
            {
                for (unsigned int i = 0;i < numEltsRegion;++i)
                {
                    double residual = 0;
                    for (unsigned int j = 0;j < NDegreesOfFreedom;++j)
                        residual += (outValue[j] - logTrsfsVector[i][j]) * (outValue[j] - logTrsfsVector[i][j]);

                    deltaVector[i] = std::exp(- residual / (m_AverageResidualValue * m_MEstimateFactor));
                }
            }
        }

        if (m_BlockDamWeights)
        {
            outValue *= damWeightsItr.Get();
            ++damWeightsItr;
        }

        outIterator.Set(outValue);
        ++outIterator;
    }
}

template <class TScalarType, unsigned int NDegreesOfFreedom, unsigned int NDimensions>
bool
MEstimateSVFImageFilter<TScalarType,NDegreesOfFreedom,NDimensions>::
checkConvergenceThreshold (OutputPixelType &outValOld, OutputPixelType &outVal)
{
    for (unsigned int i = 0;i < NDegreesOfFreedom;++i)
    {
        if (std::abs(outVal[i] - outValOld[i]) > m_ConvergenceThreshold)
            return false;
    }

    return true;
}

} // end of namespace anima
