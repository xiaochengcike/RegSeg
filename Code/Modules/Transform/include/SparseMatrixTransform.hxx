// --------------------------------------------------------------------------------------
// File:          SparseMatrixTransform.hxx
// Date:          Jun 6, 2013
// Author:        code@oscaresteban.es (Oscar Esteban)
// Version:       1.0 beta
// License:       GPLv3 - 29 June 2007
// Short Summary:
// --------------------------------------------------------------------------------------
//
// Copyright (c) 2013, code@oscaresteban.es (Oscar Esteban)
// with Signal Processing Lab 5, EPFL (LTS5-EPFL)
// and Biomedical Image Technology, UPM (BIT-UPM)
// All rights reserved.
//
// This file is part of ACWE-Reg
//
// ACWE-Reg is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// ACWE-Reg is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with ACWE-Reg.  If not, see <http://www.gnu.org/licenses/>.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//

#ifndef SPARSEMATRIXTRANSFORM_HXX_
#define SPARSEMATRIXTRANSFORM_HXX_

#include "SparseMatrixTransform.h"
#include <itkGaussianKernelFunction.h>
#include <itkBSplineKernelFunction.h>
#include <itkBSplineDerivativeKernelFunction.h>
#include <itkProgressReporter.h>

#include <itkImage.h>
#include <itkImageFileWriter.h>
#include <itkImageAlgorithm.h>

#include <vnl/algo/vnl_sparse_lu.h>
#include <vcl_vector.h>

namespace rstk {

template< class TScalar, unsigned int NDimensions >
SparseMatrixTransform<TScalar,NDimensions>
::SparseMatrixTransform():
Superclass(),
m_NumberOfSamples(0),
m_NumberOfParameters(0),
m_GridDataChanged(false),
m_ControlDataChanged(false),
m_UseImageOutput(false) {
	this->m_ControlPointsSize.Fill(10);
	this->m_ControlPointsOrigin.Fill(0.0);
	this->m_ControlPointsSpacing.Fill(1.0);
	this->m_ControlPointsDirection.SetIdentity();
	this->m_ControlPointsDirectionInverse.SetIdentity();

	this->m_Threader = itk::MultiThreader::New();
	this->m_NumberOfThreads = this->m_Threader->GetNumberOfThreads();

}

template< class TScalar, unsigned int NDimensions >
inline typename SparseMatrixTransform<TScalar,NDimensions>::ScalarType
SparseMatrixTransform<TScalar,NDimensions>
::Evaluate( const VectorType r ) const {
	ScalarType wi=1.0;
	for (size_t i = 0; i<Dimension; i++) {
		wi*= this->m_KernelFunction->Evaluate( r[i] / this->m_ControlPointsSpacing[i] );
	}
	return wi;
}

template< class TScalar, unsigned int NDimensions >
inline typename SparseMatrixTransform<TScalar,NDimensions>::ScalarType
SparseMatrixTransform<TScalar,NDimensions>
::EvaluateDerivative( const VectorType r, size_t dim ) const {
	ScalarType wi=1.0;
	for (size_t i = 0; i<Dimension; i++) {
		if( dim == i )
			wi*= this->m_DerivativeKernel->Evaluate( r[i] / this->m_ControlPointsSpacing[i] );
		else
			wi*= this->m_KernelFunction->Evaluate( r[i] / this->m_ControlPointsSpacing[i] );
	}
	return wi;
}

template< class TScalar, unsigned int NDimensions >
void
SparseMatrixTransform<TScalar,NDimensions>
::SetPhysicalDomainInformation( const DomainBase* image ) {
	for( size_t i=0; i<Dimension; i++) {
		if( this->m_ControlPointsSize[i] < 4 ){
			itkExceptionMacro( << "ControlPointsSize must be set and valid (>3) to set parameters this way.")
		}
	}

	ContinuousIndexType o_idx;
	o_idx.Fill( -0.5 );

	ContinuousIndexType e_idx;
	for ( size_t dim=0; dim< Dimension; dim++ ) {
		e_idx[dim] = image->GetLargestPossibleRegion().GetSize()[dim] - 0.5;
	}

	PointType first;
	PointType last;

	image->TransformContinuousIndexToPhysicalPoint( o_idx, first );
	image->TransformContinuousIndexToPhysicalPoint( e_idx, last );

	PointType orig,step;
	typename CoefficientsImageType::SpacingType spacing;
	for( size_t dim = 0; dim < Dimension; dim++ ) {
		step[dim] = (last[dim]-first[dim])/(1.0*this->m_ControlPointsSize[dim]);
		this->m_ControlPointsSpacing[dim] = fabs(step[dim]);
		this->m_ControlPointsOrigin[dim] = first[dim] + 0.5 * step[dim];
	}

	this->m_ControlPointsDirection = image->GetDirection();

	this->InitializeCoefficientsImages();
}

template< class TScalar, unsigned int NDimensions >
void
SparseMatrixTransform<TScalar,NDimensions>
::SetOutputReference( const DomainBase* image ) {
	this->m_UseImageOutput = true;
	this->m_OutputField = FieldType::New();
	this->m_OutputField->SetRegions( image->GetLargestPossibleRegion().GetSize() );
	this->m_OutputField->SetOrigin( image->GetOrigin() );
	this->m_OutputField->SetSpacing( image->GetSpacing() );
	this->m_OutputField->SetDirection( image->GetDirection() );
	this->m_OutputField->Allocate();
	VectorType zerov; zerov.Fill( 0.0 );
	this->m_OutputField->FillBuffer( zerov );
	this->m_NumberOfSamples = image->GetLargestPossibleRegion().GetNumberOfPixels();

	// Initialize off-grid positions
	PointType p;
	for( size_t i = 0; i < this->m_NumberOfSamples; i++ ) {
		image->TransformIndexToPhysicalPoint( image->ComputeIndex( i ), p );
		this->m_OffGridPos.push_back( p );
	}
}

template< class TScalar, unsigned int NDimensions >
void
SparseMatrixTransform<TScalar,NDimensions>
::CopyGridInformation( const DomainBase* image ) {
	this->m_ControlPointsSize      = image->GetLargestPossibleRegion().GetSize();
	this->m_ControlPointsOrigin    = image->GetOrigin();
	this->m_ControlPointsSpacing   = image->GetSpacing();
	this->m_ControlPointsDirection = image->GetDirection();
	this->InitializeCoefficientsImages();
}

template< class TScalar, unsigned int NDimensions >
void
SparseMatrixTransform<TScalar,NDimensions>
::InitializeCoefficientsImages() {
	for( size_t dim = 0; dim < Dimension; dim++ ) {
		this->m_CoefficientsImages[dim] = CoefficientsImageType::New();
		this->m_CoefficientsImages[dim]->SetRegions(   this->m_ControlPointsSize );
		this->m_CoefficientsImages[dim]->SetOrigin(    this->m_ControlPointsOrigin );
		this->m_CoefficientsImages[dim]->SetSpacing(   this->m_ControlPointsSpacing );
		this->m_CoefficientsImages[dim]->SetDirection( this->m_ControlPointsDirection );
		this->m_CoefficientsImages[dim]->Allocate();
		this->m_CoefficientsImages[dim]->FillBuffer( 0.0 );
	}

	this->m_NumberOfParameters = this->m_CoefficientsImages[0]->GetLargestPossibleRegion().GetNumberOfPixels();

	PointType p;
	CoeffImageConstPointer ref =  itkDynamicCastInDebugMode< const CoefficientsImageType* >(this->m_CoefficientsImages[0].GetPointer() );
	for( size_t i = 0; i < this->m_NumberOfParameters; i++ ) {
		ref->TransformIndexToPhysicalPoint( ref->ComputeIndex( i ), p );
		this->m_OnGridPos.push_back( p );
	}

	VectorType zerov; zerov.Fill( 0.0 );
	this->m_Field = FieldType::New();
	this->m_Field->SetRegions(   this->m_ControlPointsSize );
	this->m_Field->SetOrigin(    this->m_ControlPointsOrigin );
	this->m_Field->SetSpacing(   this->m_ControlPointsSpacing );
	this->m_Field->SetDirection( this->m_ControlPointsDirection );
	this->m_Field->Allocate();
	this->m_Field->FillBuffer( zerov );

	for( size_t i = 0; i<Dimension; i++ ) {
		VectorType zerov; zerov.Fill( 0.0 );
		this->m_Derivatives.push_back( FieldType::New() );
		this->m_Derivatives[i]->SetRegions(   this->m_ControlPointsSize );
		this->m_Derivatives[i]->SetOrigin(    this->m_ControlPointsOrigin );
		this->m_Derivatives[i]->SetSpacing(   this->m_ControlPointsSpacing );
		this->m_Derivatives[i]->SetDirection( this->m_ControlPointsDirection );
		this->m_Derivatives[i]->Allocate();
		this->m_Derivatives[i]->FillBuffer( zerov );
	}

}

template< class TScalar, unsigned int NDimensions >
void
SparseMatrixTransform<TScalar,NDimensions>
::ComputeMatrix( MatrixType type ) {
	SMTStruct str;
	str.Transform = this;
	str.type = type;
	str.vcols = &this->m_OnGridPos;

	if ( type == Self::PHI ) {
		str.vrows = &this->m_OffGridPos;

		if ( this->m_OffGridPos.size() != this->m_NumberOfSamples ) {
			itkExceptionMacro(<< "OffGrid positions are not initialized");
		}

		this->m_OffGridValueMatrix = WeightsMatrix ( this->m_NumberOfSamples, Dimension );

	} else if ( type == Self::S ) {
		str.vrows = &this->m_OnGridPos;
	} else {
		itkExceptionMacro(<< "Matrix computation not implemented" );
	}

	size_t nRows = str.vrows->size();
	size_t nCols = str.vcols->size();
	str.matrix = WeightsMatrix( nRows, nCols );

	this->GetMultiThreader()->SetNumberOfThreads( this->GetNumberOfThreads() );
	this->GetMultiThreader()->SetSingleMethod( this->ComputeThreaderCallback, &str );
	this->GetMultiThreader()->SingleMethodExecute();

	this->AfterThreadedComputeMatrix(str);
}

template< class TScalar, unsigned int NDimensions >
void
SparseMatrixTransform<TScalar,NDimensions>
::AfterThreadedComputeMatrix( SMTStruct str ) {
	if ( str.type == Self::PHI ) {
		this->m_Phi = str.matrix;
	} else if ( str.type == Self::S ) {
		this->m_S = str.matrix;
	}
}

template< class TScalar, unsigned int NDimensions >
ITK_THREAD_RETURN_TYPE
SparseMatrixTransform<TScalar,NDimensions>
::ComputeThreaderCallback(void *arg) {
	SMTStruct *str;

	itk::ThreadIdType total, threadId, threadCount;
	threadId = ( (itk::MultiThreader::ThreadInfoStruct *)( arg ) )->ThreadID;
	threadCount = ( (itk::MultiThreader::ThreadInfoStruct *)( arg ) )->NumberOfThreads;
	str = (SMTStruct *)( ( (itk::MultiThreader::ThreadInfoStruct *)( arg ) )->UserData );

	MatrixSectionType splitSection;
	splitSection.vcols = str->vcols;
	splitSection.matrix = &(str->matrix);
	splitSection.vrows = str->vrows;
	total = str->Transform->SplitMatrixSection( threadId, threadCount, splitSection );

	if( threadId < total ) {
		str->Transform->ThreadedComputeMatrix( splitSection, threadId );
	}
	return ITK_THREAD_RETURN_VALUE;
}

template< class TScalar, unsigned int NDimensions >
itk::ThreadIdType
SparseMatrixTransform<TScalar,NDimensions>
::SplitMatrixSection( itk::ThreadIdType i, itk::ThreadIdType num, MatrixSectionType& section ) {
	size_t range = section.vrows->size();

	unsigned int valuesPerThread = itk::Math::Ceil< unsigned int >(range / (double)num);
	unsigned int maxThreadIdUsed = itk::Math::Ceil< unsigned int >(range / (double)valuesPerThread) - 1;

	section.section_id = i;
	section.first_row = i * valuesPerThread;

	if( i < maxThreadIdUsed ) {
		section.num_rows = valuesPerThread;
	}
	if( i == maxThreadIdUsed ) {
		section.num_rows = range - i*valuesPerThread;
	}

	return maxThreadIdUsed + 1;
}


template< class TScalar, unsigned int NDimensions >
void
SparseMatrixTransform<TScalar,NDimensions>
::ThreadedComputeMatrix( MatrixSectionType& section, itk::ThreadIdType threadId ) {
	size_t last = section.first_row + section.num_rows;
	size_t nCols = section.vcols->size();

	itk::SizeValueType nRows = section.num_rows;

	PointsList vrows = *(section.vrows);
	PointsList vcols = *(section.vcols);

	ScalarType wi;
	PointType ci, uk;
	size_t row, col;
	VectorType r;
	VectorType support; support.Fill(1.0);

	for (size_t i = 0; i<Dimension; i++ ) support*= this->m_ControlPointsSize[i];
	double maxRadius = support.GetNorm();

	vcl_vector< int > cols;
	vcl_vector< ScalarType > vals;

	cols.resize(0);
	vals.resize(0);

	//itk::ProgressReporter progress( this, threadId, nRows, 100, 0.0f, 0.5f );

	// Walk the grid region
	for( row = section.first_row; row < last; row++ ) {
		cols.clear();
		vals.clear();

		ci = vrows[row];
		for ( col=0; col < nCols; col++) {
			uk = vcols[col];
			r = uk - ci;

			if ( ( r.GetNorm() / maxRadius ) < 2.0) {
				wi = this->Evaluate( r );

				if ( fabs(wi) > 1.0e-5) {
					cols.push_back( col );
					vals.push_back( wi );
				}
			}
		}

		if ( cols.size() > 0 ) {
			section.matrix->set_row( row, cols, vals );
		}
	}
}

template< class TScalar, unsigned int NDimensions >
typename SparseMatrixTransform<TScalar,NDimensions>::WeightsMatrix
SparseMatrixTransform<TScalar,NDimensions>
::ComputeDerivativeMatrix( PointsList vrows, PointsList vcols, size_t dim ) {
	size_t nRows = vrows.size();
	size_t nCols = vcols.size();

	WeightsMatrix phi( nRows, nCols );

	ScalarType wi;
	PointType ci, uk;
	size_t row, col;
	VectorType r;

	vcl_vector< int > cols;
	vcl_vector< ScalarType > vals;

	cols.resize(0);
	vals.resize(0);

	// Walk the grid region
	for( row = 0; row < vrows.size(); row++ ) {
		cols.clear();
		vals.clear();

		ci = vrows[row];
		for ( col=0; col < vcols.size(); col++) {
			uk = vcols[col];
			r = uk - ci;
			wi = this->EvaluateDerivative( r, dim );

			if ( fabs(wi) > 1.0e-5) {
				cols.push_back( col );
				vals.push_back( wi );
			}
		}

		if ( cols.size() > 0 ) {
			phi.set_row( row, cols, vals );
		}
	}
	return phi;
}

template< class TScalar, unsigned int NDimensions >
void
SparseMatrixTransform<TScalar,NDimensions>
::Interpolate() {
	// Check m_Phi and initializations
	if( this->m_Phi.rows() == 0 || this->m_Phi.cols() == 0 ) {
		this->ComputeMatrix( Self::PHI );
	}

	WeightsMatrix coeff = this->VectorizeCoefficients();
	this->m_OffGridValueMatrix = WeightsMatrix( this->m_NumberOfSamples, Dimension );
	this->m_Phi.mult( coeff, this->m_OffGridValueMatrix );

	SparseVectorType r;

	if( this->m_UseImageOutput ) {
		this->m_OutputField->FillBuffer( 0.0 );
		VectorType* obuf = this->m_OutputField->GetBufferPointer();
		VectorType v;
		bool setVector;

		for( size_t row = 0; row<this->m_NumberOfSamples; row++ ) {
			v.Fill( 0.0 );
			r = this->m_OffGridValueMatrix.get_row( row );
			setVector = false;

			for( size_t i = 0; i<r.size(); i++) {
				v[r[i].first] = r[i].second;
				setVector =  setVector || ( r[i].second != 0.0 );
			}
			if (setVector)
				*( obuf + row ) = v;
		}


		this->SetDisplacementField( this->m_OutputField );
	}
}

template< class TScalar, unsigned int NDimensions >
void
SparseMatrixTransform<TScalar,NDimensions>
::UpdateField() {
	if( this->m_S.rows() == 0 || this->m_S.cols() == 0 ) {
		this->ComputeMatrix( Self::S );
	}

	WeightsMatrix coeff = this->VectorizeCoefficients();
	WeightsMatrix fieldValues;

	// Interpolate
	this->m_S.mult( coeff, fieldValues );

	VectorType v;
	v.Fill( 0.0 );
	this->m_Field->FillBuffer( v );
	VectorType* fbuf = this->m_Field->GetBufferPointer();

	SparseVectorType r;
	bool setVector;

	for ( size_t row = 0; row<this->m_NumberOfParameters; row++ ) {
		setVector = false;
		r = fieldValues.get_row( row );
		for( size_t i = 0; i< r.size(); i++ ) {
			v[r[i].first] = r[i].second;
			setVector = setVector || ( r[i].second!= 0.0 );
		}

		if ( setVector )
			*( fbuf + row ) = v;
	}
}

template< class TScalar, unsigned int NDimensions >
void
SparseMatrixTransform<TScalar,NDimensions>
::ComputeCoefficients() {
	if( this->m_S.rows() == 0 || this->m_S.cols() == 0 ) {
		this->ComputeMatrix( Self::S );
	}

	DimensionParametersContainer fieldValues = this->VectorizeField( this->m_Field );
	DimensionParametersContainer coeffs;

	if ( Dimension == 3 ) {
		SolverType::Solve( this->m_S, fieldValues[0], fieldValues[1], fieldValues[2], coeffs[0], coeffs[1], coeffs[2]  );
	} else if (Dimension == 2 ) {
		SolverType::Solve( this->m_S, fieldValues[0], fieldValues[1], coeffs[0], coeffs[1]  );
	} else {
		for( size_t col = 0; col < Dimension; col++ ) {
			SolverType::Solve( this->m_S, fieldValues[col], coeffs[col] );
		}
	}

	for( size_t col = 0; col < Dimension; col++ ) {
		ScalarType* cbuffer = this->m_CoefficientsImages[col]->GetBufferPointer();
		for( size_t k = 0; k<this->m_NumberOfParameters; k++) {
			*( cbuffer + k ) = coeffs[col][k];
		}
	}

	this->Modified();
}

template< class TScalar, unsigned int NDimensions >
void
SparseMatrixTransform<TScalar,NDimensions>
::ComputeGradientField( ) {
	WeightsMatrix coeff = this->VectorizeCoefficients();
	std::vector< WeightsMatrix > gradValues;

	for( size_t i = 0; i<Dimension; i++ ) {
		if( this->m_SPrime[i].rows() == 0 || this->m_SPrime[i].cols() == 0 ) {
			this->m_SPrime[i] = this->ComputeDerivativeMatrix( this->m_OnGridPos, this->m_OnGridPos, i );
		}

		WeightsMatrix result;

		// Interpolate
		this->m_SPrime[i].mult( coeff, result );
		gradValues.push_back( result );
	}

	for( size_t i = 0; i<Dimension; i++ ){
		VectorType v;
		v.Fill( 0.0 );
		this->m_Derivatives[i]->FillBuffer( v );
		VectorType* fbuf = this->m_Derivatives[i]->GetBufferPointer();
		SparseVectorType r;

		for ( size_t row = 0; row<this->m_NumberOfParameters; row++ ) {
			for( size_t j = 0; j < Dimension; j++ ){
				r = gradValues[j].get_row( row );
				for( size_t k = 0; k< r.size(); k++ ) {
					v[r[i].first]+= r[i].second;
				}
			}
			*( fbuf + row ) = v;
		}
	}
}

template< class TScalar, unsigned int NDimensions >
typename SparseMatrixTransform<TScalar,NDimensions>::WeightsMatrix
SparseMatrixTransform<TScalar,NDimensions>
::GetPhi() {
	// Check m_Phi and initializations
	if( this->m_Phi.rows() == 0 || this->m_Phi.cols() == 0 ) {
		this->ComputeMatrix( Self::PHI );
	}

	return this->m_Phi;
}

template< class TScalar, unsigned int NDimensions >
inline void
SparseMatrixTransform<TScalar,NDimensions>
::SetOffGridPos(size_t id, typename SparseMatrixTransform<TScalar,NDimensions>::PointType pi ){
	if ( id >= this->m_NumberOfSamples ) {
		itkExceptionMacro(<< "Trying to set sample with id " << id << ", when NumberOfSamples is set to " << this->m_NumberOfSamples );
	}
	if ( this->m_OffGridPos.size() == 0 ) {
		this->m_OffGridPos.resize( this->m_NumberOfSamples );
	}
	this->m_OffGridPos[id] = pi;
}

template< class TScalar, unsigned int NDimensions >
inline size_t
SparseMatrixTransform<TScalar,NDimensions>
::AddOffGridPos(typename SparseMatrixTransform<TScalar,NDimensions>::PointType pi ){
	this->m_OffGridPos.push_back( pi );
	this->m_NumberOfSamples++;
	return (this->m_NumberOfSamples-1);
}


//template< class TScalar, unsigned int NDimensions >
//void
//SparseMatrixTransform<TScalar,NDimensions>
//::SetParameters( const ParametersType & parameters ) {
//	// Save parameters. Needed for proper operation of TransformUpdateParameters.
//	//if (&parameters != &(this->m_Parameters)) {
//	//	this->m_Parameters = parameters;
//	//}
//    //
//	//if( this->m_OffGridPos.size() == 0 ) {
//	//	itkExceptionMacro( << "Control Points should be set first." );
//	//}
//    //
//	//if( this->m_NumberOfSamples != this->m_OffGridPos.size() ) {
//	//	if (! this->m_NumberOfSamples == 0 ){
//	//		itkExceptionMacro( << "N and number of control points should match." );
//	//	} else {
//	//		this->m_NumberOfSamples = this->m_OffGridPos.size();
//	//	}
//	//}
//    //
//	//if ( this->m_NumberOfSamples != (parameters.Size() * Dimension) ) {
//	//	itkExceptionMacro( << "N and number of parameters should match." );
//	//}
//    //
//	//for ( size_t dim = 0; dim<Dimension; dim++) {
//	//	if ( this->m_OffGridValue[dim].size() == 0 ) {
//	//		this->m_OffGridValue[dim]( this->m_NumberOfSamples );
//	//	}
//	//	else if ( this->m_OffGridValue[dim].size()!=this->m_NumberOfSamples ) {
//	//		itkExceptionMacro( << "N and number of slots for parameter data should match." );
//	//	}
//	//}
//    //
//	//size_t didx = 0;
//    //
//	//while( didx < this->m_NumberOfSamples ) {
//	//	for ( size_t dim = 0; dim< Dimension; dim++ ) {
//	//		this->m_OffGridValue[dim][didx] = parameters[didx+dim];
//	//	}
//	//	didx++;
//	//}
//
//	// Modified is always called since we just have a pointer to the
//	// parameters and cannot know if the parameters have changed.
//	this->Modified();
//}


template< class TScalar, unsigned int NDimensions >
inline typename SparseMatrixTransform<TScalar,NDimensions>::VectorType
SparseMatrixTransform<TScalar,NDimensions>
::GetOffGridValue( const size_t id ) const {
	VectorType ci;
	ci.Fill( 0.0 );

	if ( !this->m_OffGridValueMatrix.empty_row( id ) ) {
		for( size_t d = 0; d < Dimension; d++) {
			ci[d] = this->m_OffGridValueMatrix( id, d );
		}
	}

	return ci;
}

//template< class TScalar, unsigned int NDimensions >
//inline typename SparseMatrixTransform<TScalar,NDimensions>::JacobianType
//SparseMatrixTransform<TScalar,NDimensions>
//::GetJacobian( const size_t id ) {
//	JacobianType gi;
//	gi.Fill( 0.0 );
//
//	for( size_t i = 0; i < Dimension; i++){
//		for( size_t j = 0; j < Dimension; j++){
//			gi(i,j) = this->m_Jacobian[j][i][id]; // Attention to transposition
//			if( std::isnan( gi(i,j) )) gi(i,j) = 0;
//		}
//	}
//	return gi;
//}

template< class TScalar, unsigned int NDimensions >
inline typename SparseMatrixTransform<TScalar,NDimensions>::VectorType
SparseMatrixTransform<TScalar,NDimensions>
::GetCoefficient( const size_t id ) {
	VectorType gi;
	for( size_t dim = 0; dim < Dimension; dim++){
		gi[dim] = this->m_CoefficientsImages[dim]->GetPixel( this->m_CoefficientsImages[dim]->ComputeIndex( id ) );
	}
	return gi;
}

//template< class TScalar, unsigned int NDimensions >
//inline typename SparseMatrixTransform<TScalar,NDimensions>::VectorType
//SparseMatrixTransform<TScalar,NDimensions>
//::GetCoeffDerivative( const size_t id ) {
//	VectorType gi;
//	for( size_t dim = 0; dim < Dimension; dim++){
//		gi[dim] = this->m_CoeffDerivative[dim][id];
//		if( std::isnan( gi[dim] )) gi[dim] = 0;
//	}
//	return gi;
//}

template< class TScalar, unsigned int NDimensions >
inline bool
SparseMatrixTransform<TScalar,NDimensions>
::SetOffGridValue( const size_t id, typename SparseMatrixTransform<TScalar,NDimensions>::VectorType pi ) {
	bool changed = false;
	for( size_t dim = 0; dim < Dimension; dim++) {
		if( std::isnan( pi[dim] )) pi[dim] = 0;
		if( this->m_OffGridValueMatrix.get(id, dim) != pi[dim] ) {
			this->m_OffGridValueMatrix.put(id, dim, pi[dim] );
			changed = true;
		}
	}
	return changed;
}

template< class TScalar, unsigned int NDimensions >
void
SparseMatrixTransform<TScalar,NDimensions>
::SetCoefficientsImages( const CoefficientsImageArray & images ) {
	for( size_t dim = 0; dim < Dimension; dim++ ) {
		CoeffImageConstPointer c = itkDynamicCastInDebugMode< const CoefficientsImageType* >( images[dim].GetPointer() );
		this->SetCoefficientsImage( dim, c );
	}
}

template< class TScalar, unsigned int NDimensions >
void
SparseMatrixTransform<TScalar,NDimensions>
::SetCoefficientsVectorImage( const FieldType* images ) {
	ScalarType* buff[Dimension];

	for( size_t dim = 0; dim < Dimension; dim++ ) {
		buff[dim] = this->m_CoefficientsImages[dim]->GetBufferPointer();
	}

	const VectorType* fbuf = images->GetBufferPointer();
	VectorType v;
	for( size_t row = 0; row < this->m_NumberOfParameters; row++ ) {
		v = *( fbuf + row );
		for( size_t dim = 0; dim < Dimension; dim++ ) {
			*( buff[dim] + row ) = v[dim];
		}
	}
}


template< class TScalar, unsigned int NDimensions >
void
SparseMatrixTransform<TScalar,NDimensions>
::SetCoefficientsImage( size_t dim, const CoefficientsImageType* c ) {
	if ( dim >= Dimension || dim < 0 ) {
		itkExceptionMacro(<< "trying to set coefficients for undefined dimension");
	}
	itk::ImageAlgorithm::Copy<CoefficientsImageType,CoefficientsImageType>(
			c, this->m_CoefficientsImages[dim],
			c->GetLargestPossibleRegion(),
			this->m_CoefficientsImages[dim]->GetLargestPossibleRegion()
	);
}

template< class TScalar, unsigned int NDimensions >
typename SparseMatrixTransform<TScalar,NDimensions>::WeightsMatrix
SparseMatrixTransform<TScalar,NDimensions>
::VectorizeCoefficients() {
	WeightsMatrix m ( this->m_NumberOfParameters, Dimension );

	std::vector< const ScalarType *> cbuffer;

	for( size_t col = 0; col<Dimension; col++)
		cbuffer.push_back( this->m_CoefficientsImages[col]->GetBufferPointer() );

	ScalarType value;
	for( size_t row = 0; row<this->m_NumberOfParameters; row++ ) {
		for( size_t col = 0; col<Dimension; col++ ) {
			value = *( cbuffer[col] + row );
			if (value!=0) {
				m.put( row, col, value );
			}
		}

	}
	return m;
}

template< class TScalar, unsigned int NDimensions >
typename SparseMatrixTransform<TScalar,NDimensions>::DimensionVector
SparseMatrixTransform<TScalar,NDimensions>
::Vectorize( const CoefficientsImageType* image ) {
	DimensionVector v( image->GetLargestPossibleRegion().GetNumberOfPixels() );
	v.fill(0.0);

	const ScalarType *cbuffer = image->GetBufferPointer();

	for( size_t row = 0; row<this->m_NumberOfParameters; row++ ) {
		v[row] = *( cbuffer + row );
	}
	return v;
}

template< class TScalar, unsigned int NDimensions >
typename SparseMatrixTransform<TScalar,NDimensions>::WeightsMatrix
SparseMatrixTransform<TScalar,NDimensions>
::MatrixField( const FieldType* image ) {
	WeightsMatrix vectorized( image->GetLargestPossibleRegion().GetNumberOfPixels(), Dimension );

	VectorType v;
	std::vector< unsigned int > cols;
	std::vector< ScalarType > vals;

	const VectorType *cbuffer = image->GetBufferPointer();
	for( size_t row = 0; row<this->m_NumberOfParameters; row++ ) {
		v = *( cbuffer + row );
		cols.clear();
		vals.clear();

		for( size_t col = 0; col<Dimension; col++) {
			if( v[col]!= 0.0 ){
				cols.push_back( col );
				vals.push_back( v[col] );
			}
		}
		if ( cols.size() > 0 ) {
			vectorized.set_row( row, cols, vals );
		}
	}
	return vectorized;
}

template< class TScalar, unsigned int NDimensions >
typename SparseMatrixTransform<TScalar,NDimensions>::DimensionParametersContainer
SparseMatrixTransform<TScalar,NDimensions>
::VectorizeField( const FieldType* image ) {
	DimensionParametersContainer vectorized;

	for( size_t col = 0; col<Dimension; col++) {
		vectorized[col] = DimensionVector( image->GetLargestPossibleRegion().GetNumberOfPixels() );
		vectorized[col].fill(0.0);
	}


	VectorType v;
	const VectorType *cbuffer = image->GetBufferPointer();

	for( size_t row = 0; row<this->m_NumberOfParameters; row++ ) {
		v = *( cbuffer + row );
		for( size_t col = 0; col<Dimension; col++) {
			vectorized[col][row] = v[col];
		}
	}
	return vectorized;
}


//template< class TScalar, unsigned int NDimensions >
//void
//SparseMatrixTransform<TScalar,NDimensions>
//::ComputeCoeffDerivatives() {
//	// Check m_Phi and initializations
//	if( this->m_Phi.rows() == 0 || this->m_Phi.cols() == 0 ) {
//		this->ComputePhi();
//	}
//
//    for ( size_t i = 0; i < Dimension; i++ ) {
//    	this->m_CoeffDerivative[i].fill(0.0);
//    	this->m_Phi.pre_mult(this->m_OffGridValue[i], this->m_CoeffDerivative[i] );
//    }
//}

}

#endif /* SPARSEMATRIXTRANSFORM_HXX_ */
