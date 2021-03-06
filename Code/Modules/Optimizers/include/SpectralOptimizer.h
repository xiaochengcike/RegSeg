// This file is part of RegSeg
//
// Copyright 2014-2017, Oscar Esteban <code@oscaresteban.es>
//
// Permission is hereby granted, free of charge, to any person
// obtaining a copy of this software and associated documentation
// files (the "Software"), to deal in the Software without
// restriction, including without limitation the rights to use,
// copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following
// conditions:
//
// The above copyright notice and this permission notice shall be
// included in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
// OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
// WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
// OTHER DEALINGS IN THE SOFTWARE.

#ifndef SPECTRALOPTIMIZER_H_
#define SPECTRALOPTIMIZER_H_

#include <boost/program_options.hpp>

#include <itkWindowConvergenceMonitoringFunction.h>
#include <vector>
#include <itkForwardFFTImageFilter.h>
#include <itkInverseFFTImageFilter.h>
#include <itkRealToHalfHermitianForwardFFTImageFilter.h>
#include <itkHalfHermitianToRealInverseFFTImageFilter.h>

#include <itkImageIteratorWithIndex.h>
#include <itkImageAlgorithm.h>
#include <itkMultiplyImageFilter.h>
#include <itkDivideImageFilter.h>
#include <itkAddImageFilter.h>


#include "rstkMacro.h"
#include "OptimizerBase.h"
#include "BSplineSparseMatrixTransform.h"

using namespace itk;
namespace bpo = boost::program_options;


namespace rstk
{
/**
 * \class SpectralOptimizer
 *  \brief Gradient descent optimizer.
 *
 * GradientDescentOptimizer implements a simple gradient descent optimizer.
 * At each iteration the current deformation field is updated according:
 * \f[
 *        u^{t+1} = \mathcal{FT^{-1}}
 * \f]
 */

template< typename TFunctional >
class SpectralOptimizer: public OptimizerBase< TFunctional >
{
public:
	/** Standard class typedefs and macros */
	typedef SpectralOptimizer                          Self;
	typedef OptimizerBase< TFunctional >               Superclass;
	typedef itk::SmartPointer<Self>                    Pointer;
	typedef itk::SmartPointer< const Self >            ConstPointer;

	itkTypeMacro( SpectralOptimizer, OptimizerBase ); // Run-time type information (and related methods)

	/* Configurable object typedefs */
	typedef typename Superclass::SettingsClass         SettingsClass;
	typedef typename Superclass::SettingsMap           SettingsMap;
	typedef typename Superclass::SettingsDesc          SettingsDesc;

	/** Metric type over which this class is templated */
	typedef TFunctional                                FunctionalType;
	typedef typename FunctionalType::ScalesType        GradientScales;
	itkStaticConstMacro( Dimension, unsigned int, FunctionalType::Dimension );

	/** Codes of stopping conditions. */
	using typename Superclass::StopConditionType;

	/** Inherited definitions */
	typedef typename Superclass::StopConditionReturnStringType  StopConditionReturnStringType;
	typedef typename Superclass::StopConditionDescriptionType   StopConditionDescriptionType;
    typedef typename Superclass::SizeValueType                  SizeValueType;
	typedef typename Superclass::ConvergenceMonitoringType	    ConvergenceMonitoringType;

	typedef typename Superclass::FunctionalPointer              FunctionalPointer;
	typedef typename Superclass::MeasureType                    MeasureType;
	typedef typename Superclass::PointType                      PointType;
	typedef typename Superclass::VectorType                     VectorType;
	typedef typename Superclass::PointValueType                 PointValueType;

	typedef typename Superclass::TransformType                  TransformType;
	typedef typename Superclass::TransformPointer               TransformPointer;
	typedef typename Superclass::CoefficientsImageType          CoefficientsImageType;
	typedef typename CoefficientsImageType::PixelType           CoefficientsValueType;
	typedef typename Superclass::CoefficientsImagePointer       CoefficientsImagePointer;
	typedef typename Superclass::CoefficientsImageArray         CoefficientsImageArray;
	typedef typename Superclass::ParametersType                 ParametersType;
	typedef typename Superclass::WeightsMatrix                  WeightsMatrix;
	typedef typename Superclass::ParametersVector				ParametersVector;
	typedef typename Superclass::ParametersPointerContainer     ParametersPointerContainer;
	typedef typename Superclass::ParametersContainer            ParametersContainer;
	typedef typename Superclass::FieldType                      FieldType;
	typedef typename Superclass::FieldPointer                   FieldPointer;
	typedef typename Superclass::FieldConstPointer              FieldConstPointer;
	typedef typename Superclass::ControlPointsGridSizeType      ControlPointsGridSizeType;
	typedef typename Superclass::ControlPointsGridSpacingType   ControlPointsGridSpacingType;

	typedef itk::MultiplyImageFilter<CoefficientsImageType, CoefficientsImageType, CoefficientsImageType> MultiplyFilterType;
	typedef itk::AddImageFilter<CoefficientsImageType, CoefficientsImageType, CoefficientsImageType>      AddFilterType;
	typedef itk::AddImageFilter<FieldType, FieldType, FieldType> AddFieldFilterType;
	typedef typename AddFieldFilterType::Pointer                 AddFieldFilterPointer;

	typedef BSplineSparseMatrixTransform
			                      < PointValueType, Dimension, 3u > SplineTransformType;
	typedef typename SplineTransformType::Pointer                   SplineTransformPointer;

	typedef itk::RealToHalfHermitianForwardFFTImageFilter
			                          <CoefficientsImageType>       FFTType;
	typedef typename FFTType::Pointer                               FFTPointer;
	typedef typename FFTType::OutputImageType                       FTDomainType;
	typedef typename FTDomainType::Pointer                          FTDomainPointer;
	typedef itk::FixedArray< FTDomainPointer, Dimension >           FTDomainArray;
	typedef typename FTDomainType::PixelType                        ComplexType;
	typedef itk::AddImageFilter<FTDomainType, FTDomainType, FTDomainType>
																	FTAddFilterType;
	typedef itk::MultiplyImageFilter<FTDomainType, FTDomainType, FTDomainType>
																	FTMultiplyFilterType;
	typedef itk::DivideImageFilter<FTDomainType, FTDomainType, FTDomainType>
																	FTDivideFilterType;

	/** Internal computation value type */
	typedef typename ComplexType::value_type                        InternalComputationValueType;
	typedef itk::Vector< InternalComputationValueType, Dimension >  InternalVectorType;
	typedef itk::Image< InternalVectorType, Dimension >             InternalVectorFieldType;
	typedef typename InternalVectorFieldType::Pointer               InternalVectorFieldPointer;
	typedef itk::ContinuousIndex< InternalComputationValueType, Dimension>
																	ContinuousIndexType;

	typedef itk::HalfHermitianToRealInverseFFTImageFilter
			        <FTDomainType, CoefficientsImageType>           IFFTType;
	typedef typename IFFTType::Pointer                              IFFTPointer;


	typedef itk::Image< InternalComputationValueType, Dimension >   RealPartType;
	typedef itk::Vector< ComplexType, Dimension >                   ComplexFieldValue;
	typedef itk::Image< ComplexFieldValue, Dimension >              ComplexFieldType;
	typedef typename ComplexFieldType::Pointer                      ComplexFieldPointer;

	itkSetMacro( Alpha, InternalVectorType );
	itkGetConstMacro( Alpha, InternalVectorType );

	itkSetMacro( Beta, InternalVectorType );
	itkGetConstMacro( Beta, InternalVectorType );

	void SetAlpha(const InternalComputationValueType v ) {
		this->m_Alpha.Fill(v);
		this->Modified();
	}

	void SetBeta(const InternalComputationValueType v ) {
//		itkExceptionMacro( << "Error: use of beta was broken during refactoring involved in https://github.com/oesteban/RegSeg/issues/228." <<
//				" Bug https://github.com/oesteban/RegSeg/issues/210 has been already reported.	")
		this->m_Beta.Fill(v);
		this->Modified();
	}

	itkSetMacro( GridSize, ControlPointsGridSizeType );
	itkSetMacro( GridSpacing, ControlPointsGridSpacingType );

	void ComputeIterationSpeed();
	MeasureType GetCurrentRegularizationEnergy() override;
	MeasureType GetCurrentEnergy() override;

	itkGetConstObjectMacro(CurrentCoefficients, FieldType);

	const FieldType * GetCurrentCoefficientsField () const override {
		return this->m_Transform->GetCoefficientsField();
	}

	static void AddOptions( SettingsDesc& opts );
protected:
	SpectralOptimizer();
	~SpectralOptimizer() {}
	void PrintSelf( std::ostream &os, itk::Indent indent ) const override;

	/* Inherited from OptimizerBase */
	virtual void ComputeDerivative() override;
	virtual void Iterate() override = 0;
	virtual void PostIteration() override;
	void InitializeParameters() override;
	virtual void InitializeAuxiliarParameters() override = 0;

	/* SpectralOptimizer specific members */
	void ComputeUpdate(CoefficientsImageArray uk,
			           const CoefficientsImageArray gk,
			           CoefficientsImageArray next_uk,
			           bool changeDirection = false );
	void BetaRegularization(CoefficientsImagePointer numerator,
			                CoefficientsImageArray next_uk,
			                InternalComputationValueType s,
			                size_t d);

	virtual void SetUpdate() = 0;

	virtual void ParseSettings() override;

	/* Common variables for optimization control and reporting */
	bool                          m_DenominatorCached;

	/** Particular parameter definitions from our method */
	InternalVectorType m_Alpha;
	InternalVectorType m_Beta;

	/* Energy tracking */
	MeasureType m_RegularizationEnergy;
	MeasureType m_CurrentTotalEnergy;
	bool m_RegularizationEnergyUpdated;

	CoefficientsImageArray       m_NextCoefficients;
	CoefficientsImageArray       m_Denominator;
	FTDomainPointer              m_FTLaplacian;
	FTDomainPointer              m_FTOnes;
	FieldPointer                 m_LastCoeff;
	FieldPointer                 m_CurrentCoefficients;
	AddFieldFilterPointer        m_FieldCoeffAdder;
private:
	SpectralOptimizer( const Self & ); // purposely not implemented
	void operator=( const Self & ); // purposely not implemented

	void ApplyRegularizationTerm( ComplexFieldType* reference );
	void ApplyRegularizationComponent( size_t d, FTDomainType *reference );
	void InitializeDenominator( itk::ImageBase<Dimension> *reference );
	void UpdateField();
}; // End of Class

} // End of namespace rstk

#ifndef ITK_MANUAL_INSTANTIATION
#include "SpectralOptimizer.hxx"
#endif


#endif /* SPECTRALOPTIMIZER_H_ */
