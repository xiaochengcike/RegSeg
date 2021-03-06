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

#ifndef ACWEREGISTRATIONMETHOD_HXX_
#define ACWEREGISTRATIONMETHOD_HXX_

#include "ACWERegistrationMethod.h"

#include <boost/lexical_cast.hpp>
#include <algorithm>    // std::fill

namespace rstk {

template < typename TFixedImage, typename TTransform, typename TComputationalValue >
ACWERegistrationMethod< TFixedImage, TTransform, TComputationalValue >
::ACWERegistrationMethod(): m_NumberOfLevels(0),
 	 	 	 	 	 	 	m_CurrentLevel(0),
 	 	 	 	 	 	 	m_OutputPrefix(""),
                            m_UseGridLevelsInitialization(false),
                            m_UseGridSizeInitialization(true),
                            m_UseCustomGridSize(false),
                            m_Initialized(false),
                            m_AutoSmoothing(false),
                            m_Stop(false),
                            m_Verbosity(1),
                            m_TransformNumberOfThreads(0) {
	this->m_StopCondition      = ALL_LEVELS_DONE;
	this->m_StopConditionDescription << this->GetNameOfClass() << ": ";

	this->SetNumberOfRequiredOutputs( 1 );

	this->m_OutputTransform = OutputTransformType::New();
	this->m_OutputInverseTransform = OutputTransformType::New();
	DecoratedOutputTransformPointer transformDecorator = DecoratedOutputTransformType::New().GetPointer();
	transformDecorator->Set( this->m_OutputTransform );
	this->ProcessObject::SetNthOutput( 0, transformDecorator );

	this->m_MinGridSize.Fill( 4 );

	this->m_JSONRoot = JSONRoot( Json::arrayValue );
}

template < typename TFixedImage, typename TTransform, typename TComputationalValue >
void
ACWERegistrationMethod< TFixedImage, TTransform, TComputationalValue >
::PrintSelf( std::ostream &os, itk::Indent indent ) const {
	Superclass::PrintSelf(os,indent);
}

template < typename TFixedImage, typename TTransform, typename TComputationalValue >
void
ACWERegistrationMethod< TFixedImage, TTransform, TComputationalValue >
::Initialize() {
	if ( ! m_Initialized ) {
		//
		// this->GenerateSchedule();
	}
	m_Initialized = true;
}


template < typename TFixedImage, typename TTransform, typename TComputationalValue >
void
ACWERegistrationMethod< TFixedImage, TTransform, TComputationalValue >
::GenerateData() {
	this->InvokeEvent( itk::StartEvent() );

	size_t nPriors = this->m_PriorsNames.size();
	this->Initialize();

	while( this->m_CurrentLevel < this->m_NumberOfLevels ) {
		std::cout << "Starting registration level " << this->m_CurrentLevel << "." << std::endl;
		try {
			this->SetUpLevel( this->m_CurrentLevel );
		} catch ( itk::ExceptionObject & err ) {
			this->Stop( LEVEL_SETTING_ERROR,  "Error while setting up level "
					+  boost::lexical_cast<std::string>(this->m_CurrentLevel) );
			throw err;  // Pass exception to caller
		}

		try {
			m_Optimizer->Start();
		} catch ( itk::ExceptionObject & err ) {
			this->Stop( LEVEL_PROCESS_ERROR, "Error while executing level "
					+ boost::lexical_cast<std::string>(this->m_CurrentLevel));
			throw err;  // Pass exception to caller
		}

		// Add JSON tree to the general logging facility
		this->m_JSONRoot.append( this->m_CurrentLogger->GetJSONRoot() );
		this->m_OutputTransform->PushBackTransform(this->m_Optimizer->GetTransform());

		this->m_CurrentContours.resize(nPriors);
		for (size_t i = 0; i < nPriors; i++ ) {
			Shape2PriorCopyPointer copy = Shape2PriorCopyType::New();
			copy->SetInput( this->m_Functional->GetCurrentContours()[i] );
			copy->Update();
			this->m_CurrentContours[i] = copy->GetOutput();
		}

		this->InvokeEvent( itk::IterationEvent() );

		if ( this->m_CurrentLevel == this->m_NumberOfLevels - 1 ) {
			this->Stop( ALL_LEVELS_DONE, "All levels are finished ("
					+ boost::lexical_cast<std::string>(this->m_NumberOfLevels) + " levels)." );
			break;
		}

		this->m_Functional = NULL;
		this->m_Optimizer = NULL;

		this->m_CurrentLevel++;
	}

    this->GenerateFinalDisplacementField();
}

template < typename TFixedImage, typename TTransform, typename TComputationalValue >
void
ACWERegistrationMethod< TFixedImage, TTransform, TComputationalValue >
::SetUpLevel( size_t level ) {
	if( level > (this->m_NumberOfLevels-1) ) {
		itkExceptionMacro( << "Trying to set up a level beyond NumberOfLevels (level=" << (level+1) << ")." );
	}

	this->m_Functional = FunctionalType::New();
	this->m_Functional->SetSettings( this->m_Config[level] );
	this->m_Functional->LoadReferenceImage( this->m_ReferenceNames );

	if (this->m_FixedMask.IsNotNull() ) {
		this->m_Functional->SetBackgroundMask(this->m_FixedMask);
	}

	if ( level == 0 ) {
		this->m_Functional->LoadShapePriors( this->m_PriorsNames );
	} else {
		for ( size_t i = 0; i<this->m_PriorsNames.size(); i++ ) {
			this->m_Functional->AddShapePrior( this->m_CurrentContours[i] );
		}
		this->m_CurrentContours.clear();
	}

	// Add targets (if requested, testing purposes)
	for ( size_t i = 0; i<this->m_Target.size(); i++ ) {
		this->m_Functional->AddShapeTarget( this->m_Target[i] );
	}

	// Connect Optimizer
	this->m_Optimizer = DefaultOptimizerType::New();
	this->m_Optimizer->SetFunctional( this->m_Functional );
	this->m_Optimizer->SetSettings( this->m_Config[level] );

	if ( this->m_TransformNumberOfThreads > 0 ) {
		this->m_Optimizer->GetTransform()->SetNumberOfThreads( this->m_TransformNumberOfThreads );
	}

	this->m_CurrentLogger = JSONLoggerType::New();
	this->m_CurrentLogger->SetOptimizer( this->m_Optimizer );
	this->m_CurrentLogger->SetLevel( level );

	if( this->m_Verbosity > 0 ) {
		this->m_ImageLogger = IterationWriterUpdate::New();
		this->m_ImageLogger->SetOptimizer( this->m_Optimizer );
		this->m_ImageLogger->SetPrefix( this->m_OutputPrefix );
		this->m_ImageLogger->SetLevel( level );
		this->m_ImageLogger->SetVerbosity( this->m_Verbosity );

		this->m_OutLogger = STDOutLoggerType::New();
		this->m_OutLogger->SetOptimizer( this->m_Optimizer );
		this->m_OutLogger->SetLevel( level );
	}


}

template < typename TFixedImage, typename TTransform, typename TComputationalValue >
void
ACWERegistrationMethod< TFixedImage, TTransform, TComputationalValue >
::SetNumberOfLevels( size_t levels ) {
	if( levels == 0 || levels > 15 ) {
		itkExceptionMacro( << "intended NumberOfLevels is not valid (zero or >15).")
	}
	this->m_NumberOfLevels = levels;

	m_GridSchedule.resize(m_NumberOfLevels);
	m_NumberOfIterations.resize( this->m_NumberOfLevels );
	m_StepSize.resize( this->m_NumberOfLevels );
	m_Alpha.resize( this->m_NumberOfLevels );
	m_Beta.resize( this->m_NumberOfLevels );
	m_DescriptorRecomputationFreq.resize( this->m_NumberOfLevels );
	m_Config.resize( this->m_NumberOfLevels );
}

template < typename TFixedImage, typename TTransform, typename TComputationalValue >
void
ACWERegistrationMethod< TFixedImage, TTransform, TComputationalValue >
::Stop( StopConditionType code, std::string msg ) {
	this->m_StopConditionDescription << msg;
	this->m_StopCondition = code;
	itkDebugMacro( "Stop called with a description - "  << this->m_StopConditionDescription.str() );
	this->m_Stop = true;
	this->InvokeEvent( itk::EndEvent() );
}

template < typename TFixedImage, typename TTransform, typename TComputationalValue >
void
ACWERegistrationMethod< TFixedImage, TTransform, TComputationalValue >
::ConcatenateFields( size_t level ) {
	ReferenceImageConstPointer refim = this->GetFixedImage();

	OutputVectorType zerov;
	zerov.Fill(0.0);

	this->m_DisplacementField = OutputFieldType::New();
	this->m_DisplacementField->SetOrigin( refim->GetOrigin() );
	this->m_DisplacementField->SetDirection( refim->GetDirection() );
	this->m_DisplacementField->SetRegions( refim->GetLargestPossibleRegion() );
	this->m_DisplacementField->SetSpacing( refim->GetSpacing() );
	this->m_DisplacementField->Allocate();
	this->m_DisplacementField->FillBuffer(0.0);
	OutputVectorType* outbuff = this->m_DisplacementField->GetBufferPointer();

	this->m_InverseDisplacementField = OutputFieldType::New();
	this->m_InverseDisplacementField->SetOrigin( refim->GetOrigin() );
	this->m_InverseDisplacementField->SetDirection( refim->GetDirection() );
	this->m_InverseDisplacementField->SetRegions( refim->GetLargestPossibleRegion() );
	this->m_InverseDisplacementField->SetSpacing( refim->GetSpacing() );
	this->m_InverseDisplacementField->Allocate();
	this->m_InverseDisplacementField->FillBuffer(0.0);
	OutputVectorType* outinvbuff = this->m_InverseDisplacementField->GetBufferPointer();

	const OutputVectorType* tfbuff[level];

	//for( size_t i = 0; i < level; i++ ) {
	//	//this->m_Transforms[i]->Interpolate();
	//	//tfbuff[i] = this->m_Transforms[i]->GetDisplacementField()->GetBufferPointer();
	//}

	size_t nPix = this->m_DisplacementField->GetLargestPossibleRegion().GetNumberOfPixels();

	for( size_t i = 0; i < nPix; i++ ) {
		for ( size_t j = 0; j < level; j++) {
			*( outbuff + i ) += *( tfbuff[j] + i );
			*( outinvbuff + i ) -= *( tfbuff[j] + i );
		}
	}
}

template < typename TFixedImage, typename TTransform, typename TComputationalValue >
void
ACWERegistrationMethod< TFixedImage, TTransform, TComputationalValue >
::GenerateFinalDisplacementField() {
	this->m_OutputTransform->SetOutputReference(this->m_Functional->GetReferenceImage());
	this->m_OutputTransform->Interpolate();
	this->m_DisplacementField = this->m_OutputTransform->GetDisplacementField();
}

/*
 *  Get output transform
 */
template < typename TFixedImage, typename TTransform, typename TComputationalValue >
const typename ACWERegistrationMethod< TFixedImage, TTransform, TComputationalValue >::DecoratedOutputTransformType *
ACWERegistrationMethod< TFixedImage, TTransform, TComputationalValue >
::GetOutput() const
{
  return static_cast<const DecoratedOutputTransformType *>( this->ProcessObject::GetOutput( 0 ) );
}


template < typename TFixedImage, typename TTransform, typename TComputationalValue >
void
ACWERegistrationMethod< TFixedImage, TTransform, TComputationalValue >
::SetSettingsOfLevel( size_t l, SettingsMap& map) {
	if( l >= this->m_Config.size() ) {
		itkExceptionMacro( << "settings of level " << l << " are not initialized.");
	}

	this->m_Config[l] = map;
	this->Modified();
}


template < typename TFixedImage, typename TTransform, typename TComputationalValue >
typename ACWERegistrationMethod< TFixedImage, TTransform, TComputationalValue >::FieldList
ACWERegistrationMethod< TFixedImage, TTransform, TComputationalValue >
::GetCoefficientsField() {
	this->m_CoefficientsContainer.clear();

	for (size_t i = 0; i<this->m_NumberOfLevels; i++) {
		this->m_CoefficientsContainer.push_back(static_cast<const FieldType* >(this->m_Optimizers[i]->GetCurrentCoefficientsField()));
	}
	return this->m_CoefficientsContainer;
}


} // namespace rstk

#endif /* ACWEREGISTRATIONMETHOD_HXX_ */
