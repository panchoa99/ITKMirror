/*=========================================================================

  Program:   Insight Segmentation & Registration Toolkit
  Module:    itkMutualInformationImageToImageMetric.txx
  Language:  C++
  Date:      $Date$
  Version:   $Revision$

  Copyright (c) 2002 Insight Consortium. All rights reserved.
  See ITKCopyright.txt or http://www.itk.org/HTML/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even 
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR 
     PURPOSE.  See the above copyright notices for more information.

=========================================================================*/
#ifndef _itkMutualInformationImageToImageMetric_txx
#define _itkMutualInformationImageToImageMetric_txx

#include "itkMutualInformationImageToImageMetric.h"
#include "itkCovariantVector.h"
#include "vnl/vnl_sample.h"
#include "vnl/vnl_math.h"
#include "itkGaussianKernelFunction.h"

namespace itk
{

/**
 * Constructor
 */
template < class TFixedImage, class TMovingImage >
MutualInformationImageToImageMetric<TFixedImage,TMovingImage>
::MutualInformationImageToImageMetric()
{

  m_NumberOfSpatialSamples = 0;
  this->SetNumberOfSpatialSamples( 50 );

  m_KernelFunction  = dynamic_cast<KernelFunction*>(
    GaussianKernelFunction::New().GetPointer() );

  m_FixedImageStandardDeviation = 0.4;
  m_MovingImageStandardDeviation = 0.4;

  m_MinProbability = 0.0001;

  //
  // Following initialization is related to
  // calculating image derivatives
  m_DerivativeCalculator = DerivativeFunctionType::New();

}


template < class TFixedImage, class TMovingImage  >
void
MutualInformationImageToImageMetric<TFixedImage,TMovingImage>
::PrintSelf(std::ostream& os, Indent indent) const
{
  Superclass::PrintSelf(os, indent);
  os << indent << "NumberOfSpatialSamples: ";
  os << m_NumberOfSpatialSamples << std::endl;
  os << indent << "FixedImageStandardDeviation: ";
  os << m_FixedImageStandardDeviation << std::endl;
  os << indent << "MovingImageStandardDeviation: ";
  os << m_MovingImageStandardDeviation << std::endl;
  os << indent << "KernelFunction: ";
  os << m_KernelFunction.GetPointer() << std::endl;
}


/**
 * Set the number of spatial samples
 */
template < class TFixedImage, class TMovingImage  >
void
MutualInformationImageToImageMetric<TFixedImage,TMovingImage>
::SetNumberOfSpatialSamples( 
unsigned int num )
{
  if ( num == m_NumberOfSpatialSamples ) return;

  this->Modified();
 
  // clamp to minimum of 1
  m_NumberOfSpatialSamples = ((num > 1) ? num : 1 );

  // resize the storage vectors
  m_SampleA.resize( m_NumberOfSpatialSamples );
  m_SampleB.resize( m_NumberOfSpatialSamples );

}


/**
 * Uniformly sample the fixed image domain. Each sample consists of:
 *  - the fixed image value
 *  - the corresponding moving image value
 */
template < class TFixedImage, class TMovingImage  >
void
MutualInformationImageToImageMetric<TFixedImage,TMovingImage>
::SampleFixedImageDomain(
SpatialSampleContainer& samples )
{

  double range =
   double( m_FixedImage->GetBufferedRegion().GetNumberOfPixels() ) - 1.0;

  typename SpatialSampleContainer::iterator iter;
  typename SpatialSampleContainer::const_iterator end = samples.end();

  bool allOutside = true;

  for( iter = samples.begin(); iter != end; ++iter )
    {
    // generate a random number between [0,range)
    unsigned long offset = (unsigned long) vnl_sample_uniform( 0.0, range );

    // translate offset to index in the fixed image domain
    FixedImageIndexType index = m_FixedImage->ComputeIndex( offset );

    // get fixed image value
    (*iter).FixedImageValue = m_FixedImage->GetPixel( index );

    // get moving image value
    m_FixedImage->TransformIndexToPhysicalPoint( index, 
      (*iter).FixedImagePointValue );

    MovingImagePointType mappedPoint = 
      m_Transform->TransformPoint( (*iter).FixedImagePointValue );

    if( m_Interpolator->IsInsideBuffer( mappedPoint ) )
      {
      (*iter).MovingImageValue = m_Interpolator->Evaluate( mappedPoint );
      allOutside = false;
      }
    else
      {
      (*iter).MovingImageValue = 0;
      }

    }

  if( allOutside )
    {
    // if all the samples mapped to the outside throw an exception
    ExceptionObject err(__FILE__, __LINE__);
    err.SetLocation( "MutualInformationImageToImageMetric" );
    err.SetDescription( "All the sampled point mapped to outside of the reference image" );
    throw err;
    }

}


/**
 * Get the match Measure
 */
template < class TFixedImage, class TMovingImage  >
MutualInformationImageToImageMetric<TFixedImage,TMovingImage>
::MeasureType
MutualInformationImageToImageMetric<TFixedImage,TMovingImage>
::GetValue( const ParametersType& parameters )
{

  if( !m_FixedImage || !m_Interpolator || !m_Transform )
    {
    m_MatchMeasure = 0;
    return m_MatchMeasure;
    }

  // make sure the transform has the current parameters
  m_Transform->SetParameters( parameters );

  // collect sample set A
  this->SampleFixedImageDomain( m_SampleA );

  // collect sample set B
  this->SampleFixedImageDomain( m_SampleB );

  // calculate the mutual information
  double dLogSumTarget = 0.0;
  double dLogSumRef    = 0.0;
  double dLogSumJoint  = 0.0;

  typename SpatialSampleContainer::const_iterator aiter;
  typename SpatialSampleContainer::const_iterator aend = m_SampleA.end();
  typename SpatialSampleContainer::const_iterator biter;
  typename SpatialSampleContainer::const_iterator bend = m_SampleB.end();

  for( biter = m_SampleB.begin() ; biter != bend; ++biter )
    {
    double dSumTarget  = m_MinProbability;
    double dSumRef     = m_MinProbability;
    double dSumJoint   = m_MinProbability;

    for( aiter = m_SampleA.begin() ; aiter != aend; ++aiter )
      {
      double valueTarget;
      double valueRef;

      valueTarget = ( (*biter).FixedImageValue - (*aiter).FixedImageValue ) /
        m_FixedImageStandardDeviation;
      valueTarget = m_KernelFunction->Evaluate( valueTarget );

      valueRef = ( (*biter).MovingImageValue - (*aiter).MovingImageValue ) /
        m_MovingImageStandardDeviation;
      valueRef = m_KernelFunction->Evaluate( valueRef );

      dSumTarget += valueTarget;
      dSumRef    += valueRef;
      dSumJoint  += valueTarget * valueRef;

      } // end of sample A loop

    dLogSumTarget -= log( dSumTarget );
    dLogSumRef    -= log( dSumRef );
    dLogSumJoint  -= log( dSumJoint );

    } // end of sample B loop

  double nsamp   = double( m_NumberOfSpatialSamples );

  double threshold = -0.5 * nsamp * log( m_MinProbability );
  if( dLogSumRef > threshold || dLogSumTarget > threshold ||
      dLogSumJoint > threshold  )
    {
    // at least half the samples in B did not occur within
    // the Parzen window width of samples in A
    ExceptionObject err(__FILE__, __LINE__);
    err.SetLocation( "MutualInformationImageToImageMetric" );
    err.SetDescription( "Standard deviation is too small" );
    throw err;
    }

  m_MatchMeasure = dLogSumTarget + dLogSumRef - dLogSumJoint;
  m_MatchMeasure /= nsamp;
  m_MatchMeasure += log( nsamp );

  return m_MatchMeasure;

}


/**
 * Get the both Value and Derivative Measure
 */
template < class TFixedImage, class TMovingImage  >
void
MutualInformationImageToImageMetric<TFixedImage,TMovingImage>
::GetValueAndDerivative(
const ParametersType& parameters,
MeasureType& value,
DerivativeType& derivative)
{

  // reset the derivatives all to zero
  m_MatchMeasureDerivatives.Fill(0);
  m_MatchMeasure = 0;

  // check if inputs are valid
  if( !m_FixedImage || !m_MovingImage || !m_Interpolator || !m_Transform )
    {
    value = m_MatchMeasure;
    derivative = m_MatchMeasureDerivatives;
    return;
    }

  // make sure the transform has the current parameters
  m_Transform->SetParameters( parameters );
  unsigned int numberOfParameters = m_Transform->GetNumberOfParameters();
  m_MatchMeasureDerivatives = DerivativeType( numberOfParameters );
  m_MatchMeasureDerivatives.Fill( 0 );

  // set the DerivativeCalculator
  m_DerivativeCalculator->SetInputImage( m_MovingImage );

  // collect sample set A
  this->SampleFixedImageDomain( m_SampleA );

  // collect sample set B
  this->SampleFixedImageDomain( m_SampleB );


  // calculate the mutual information
  double dLogSumTarget = 0.0;
  double dLogSumRef    = 0.0;
  double dLogSumJoint  = 0.0;

  typename SpatialSampleContainer::iterator aiter;
  typename SpatialSampleContainer::const_iterator aend = m_SampleA.end();
  typename SpatialSampleContainer::iterator biter;
  typename SpatialSampleContainer::const_iterator bend = m_SampleB.end();

  // precalculate all the image derivatives for sample A
  typedef typename std::vector<DerivativeType> DerivativeContainer;
  DerivativeContainer sampleADerivatives;
  sampleADerivatives.resize( m_NumberOfSpatialSamples );

  typename DerivativeContainer::iterator aditer;
  DerivativeType tempDeriv( numberOfParameters );

  for( aiter = m_SampleA.begin(), aditer = sampleADerivatives.begin();
    aiter != aend; ++aiter, ++aditer )
    {
    /**** FIXME: is there a way to avoid the extra copying step? *****/
    this->CalculateDerivatives( (*aiter).FixedImagePointValue, tempDeriv );
    (*aditer) = tempDeriv;
    }


  DerivativeType derivB(numberOfParameters);

  for( biter = m_SampleB.begin(); biter != bend; ++biter )
    {
    double dDenominatorRef = m_MinProbability;
    double dDenominatorJoint = m_MinProbability;

    double dSumTarget = m_MinProbability;

    for( aiter = m_SampleA.begin(); aiter != aend; ++aiter )
      {
      double valueTarget;
      double valueRef;

      valueTarget = ( (*biter).FixedImageValue - (*aiter).FixedImageValue )
        / m_FixedImageStandardDeviation;
      valueTarget = m_KernelFunction->Evaluate( valueTarget );

      valueRef = ( (*biter).MovingImageValue - (*aiter).MovingImageValue )
        / m_MovingImageStandardDeviation;
      valueRef = m_KernelFunction->Evaluate( valueRef );

      dDenominatorRef += valueRef;
      dDenominatorJoint += valueRef * valueTarget;

      dSumTarget += valueTarget;

      } // end of sample A loop

    dLogSumTarget -= log( dSumTarget );
    dLogSumRef    -= log( dDenominatorRef );
    dLogSumJoint  -= log( dDenominatorJoint );

    // get the image derivative for this B sample
    this->CalculateDerivatives( (*biter).FixedImagePointValue, derivB );

    for( aiter = m_SampleA.begin(), aditer = sampleADerivatives.begin();
      aiter != aend; ++aiter, ++aditer )
      {
      double valueTarget;
      double valueRef;
      double weightRef;
      double weightJoint;
      double weight;

      valueTarget = ( (*biter).FixedImageValue - (*aiter).FixedImageValue ) /
        m_FixedImageStandardDeviation;
      valueTarget = m_KernelFunction->Evaluate( valueTarget );

      valueRef = ( (*biter).MovingImageValue - (*aiter).MovingImageValue ) /
        m_MovingImageStandardDeviation;
      valueRef = m_KernelFunction->Evaluate( valueRef );

      weightRef = valueRef / dDenominatorRef;
      weightJoint = valueRef * valueTarget / dDenominatorJoint;

      weight = ( weightRef - weightJoint );
      weight *= (*biter).MovingImageValue - (*aiter).MovingImageValue;

      m_MatchMeasureDerivatives += ( derivB - (*aditer) ) * weight;

      } // end of sample A loop

    } // end of sample B loop


  double nsamp    = double( m_NumberOfSpatialSamples );

  double threshold = -0.5 * nsamp * log( m_MinProbability );
  if( dLogSumRef > threshold || dLogSumTarget > threshold ||
      dLogSumJoint > threshold  )
    {
    // at least half the samples in B did not occur within
    // the Parzen window width of samples in A
    ExceptionObject err(__FILE__, __LINE__);
    err.SetLocation( "MutualInformationImageToImageMetric" );
    err.SetDescription( "Standard deviation is too small" );
    throw err;
    }

  m_MatchMeasure  = dLogSumTarget + dLogSumRef - dLogSumJoint;
  m_MatchMeasure /= nsamp;
  m_MatchMeasure += log( nsamp );

  m_MatchMeasureDerivatives /= nsamp;
  m_MatchMeasureDerivatives /= vnl_math_sqr( m_MovingImageStandardDeviation );

  value = m_MatchMeasure;
  derivative =  m_MatchMeasureDerivatives;

}


/**
 * Get the match measure derivative
 */
template < class TFixedImage, class TMovingImage  >
const 
MutualInformationImageToImageMetric<TFixedImage,TMovingImage>
::DerivativeType&
MutualInformationImageToImageMetric<TFixedImage,TMovingImage>
::GetDerivative( const ParametersType& parameters )
{
  MeasureType value;
  DerivativeType deriv;
  // call the combined version
  this->GetValueAndDerivative( parameters, value, deriv );

  return m_MatchMeasureDerivatives;
}


/**
 * Calculate derivatives of the image intensity with respect
 * to the transform parmeters.
 *
 * This should really be done by the mapper.
 *
 * This is a temporary solution until this feature is implemented
 * in the mapper. This solution only works for any transform
 * that support GetJacobian()
 */
template < class TFixedImage, class TMovingImage  >
void
MutualInformationImageToImageMetric<TFixedImage,TMovingImage>
::CalculateDerivatives(
const FixedImagePointType& point,
DerivativeType& derivatives )
{

  MovingImagePointType mappedPoint = m_Transform->TransformPoint( point );
  
  /*** FIXME: figure how to do this with the image's PhysicalToIndexTransform.
   Problem: can't GetPhysicalToIndexTransform because it is not const and
   this metic holds a const reference to the moving image ******/
  MovingImageIndexType mappedIndex; 
  for( unsigned int j = 0; j < MovingImageDimension; j++ )
    {
    mappedIndex[j] = static_cast<long>( vnl_math_rnd( ( mappedPoint[j] - 
      m_MovingImage->GetOrigin()[j] ) / m_MovingImage->GetSpacing()[j] ) );
    }

  CovariantVector<double,MovingImageDimension> imageDerivatives;
  for ( int j = 0; j < MovingImageDimension; j++ )
    {
    imageDerivatives[j] = 
      m_DerivativeCalculator->EvaluateAtIndex( mappedIndex, j );
    }

  typedef typename TransformType::JacobianType JacobianType;
  const JacobianType& jacobian = m_Transform->GetJacobian( point );

  unsigned int numberOfParameters = m_Transform->GetNumberOfParameters();

  for ( unsigned int k = 0; k < numberOfParameters; k++ )
    {
    derivatives[k] = 0.0;
    for ( int j = 0; j < MovingImageDimension; j++ )
      {
      derivatives[k] += jacobian[j][k] * imageDerivatives[j];
      }
    } 

}


} // end namespace itk


#endif

