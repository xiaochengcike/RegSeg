// --------------------------------------------------------------------------------------
// File:          MahalanobisDistanceModel.hxx
// Date:          Dec 23, 2014
// Author:        code@oscaresteban.es (Oscar Esteban)
// Version:       1.0 beta
// License:       GPLv3 - 29 June 2007
// Short Summary:
// --------------------------------------------------------------------------------------
//
// Copyright (c) 2014, code@oscaresteban.es (Oscar Esteban)
// with Signal Processing Lab 5, EPFL (LTS5-EPFL)
// and Biomedical Image Technology, UPM (BIT-UPM)
// All rights reserved.
//
// This file is part of ACWEReg
//
// ACWEReg is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// ACWEReg is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with ACWEReg.  If not, see <http://www.gnu.org/licenses/>.
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

#ifndef _MAHALANOBISDISTANCEMODEL_HXX_
#define _MAHALANOBISDISTANCEMODEL_HXX_

#include "MahalanobisDistanceModel.h"


#include <math.h>
#include <vnl/vnl_math.h>
#include <vnl/vnl_matrix.h>
#include <vnl/vnl_diag_matrix.h>
#include <vnl/algo/vnl_symmetric_eigensystem.h>
#include <vnl/algo/vnl_ldl_cholesky.h>


namespace rstk {
template< typename TInputVectorImage, typename TPriorsPrecisionType >
MahalanobisDistanceModel< TInputVectorImage, TPriorsPrecisionType >
::MahalanobisDistanceModel():
  Superclass() {
}

template< typename TInputVectorImage, typename TPriorsPrecisionType >
void
MahalanobisDistanceModel< TInputVectorImage, TPriorsPrecisionType >
::PrintSelf(std::ostream & os, itk::Indent indent) const {
	Superclass::PrintSelf(os, indent);
}

template< typename TInputVectorImage, typename TPriorsPrecisionType >
void
MahalanobisDistanceModel< TInputVectorImage, TPriorsPrecisionType >
::InitializeMemberships() {
	this->m_NumberOfRegions = this->GetPriorsMap()->GetNumberOfComponentsPerPixel();
	this->m_Memberships.resize(this->m_NumberOfRegions);

	size_t nregions = this->m_NumberOfRegions - 1;
	this->m_Means.resize(nregions);
	this->m_Covariances.resize(nregions);
	this->m_RegionOffsetContainer.SetSize(nregions);
	this->m_RegionOffsetContainer.Fill(0.0);
}

template< typename TInputVectorImage, typename TPriorsPrecisionType >
void
MahalanobisDistanceModel< TInputVectorImage, TPriorsPrecisionType >
::GenerateData() {
	this->InitializeMemberships();
	this->EstimateRobust();
}

template< typename TInputVectorImage, typename TPriorsPrecisionType >
void
MahalanobisDistanceModel< TInputVectorImage, TPriorsPrecisionType >
::Estimate() {
	ReferenceSamplePointer sample = ReferenceSampleType::New();
	sample->SetImage( this->GetInput() );
	size_t npix = sample->Size();
	size_t nregions = this->m_NumberOfRegions - 1;

	const PriorsPrecisionType* priors = this->GetPriorsMap()->GetBufferPointer();
	size_t offset = this->GetPriorsMap()->GetNumberOfComponentsPerPixel();

	std::vector<WeightArrayType> weights;
	for( size_t roi = 0; roi < nregions; roi++ ) {
		WeightArrayType warr;
		warr.SetSize(npix);
		warr.Fill(0.0);
		weights.push_back(warr);
	}

	PriorsPrecisionType w;
	for( size_t i = 0; i < npix; i++ ) {
		for( size_t roi = 0; roi < nregions; roi++ ) {
			w = *(priors + offset * i + roi);
			weights[roi][i] = w;
		}
	}

	for( size_t roi = 0; roi < nregions; roi++ ) {
		CovarianceFilterPointer covFilter = CovarianceFilter::New();
		covFilter->SetInput( sample );
		covFilter->SetWeights( weights[roi] );
		covFilter->Update();

		InternalFunctionPointer mf = InternalFunctionType::New();
		mf->SetMean( covFilter->GetMean() );

		CovarianceMatrixType cov = covFilter->GetCovarianceMatrix();
		mf->SetCovariance( cov );

		this->m_Memberships[roi] = mf;
		this->m_RegionOffsetContainer[roi] = log(2.0 * vnl_math::pi) * cov.Rows() + this->ComputeCovarianceDeterminant(cov);
		this->m_Means[roi] = covFilter->GetMean();
		this->m_Covariances[roi] = cov;
	}
}

template< typename TInputVectorImage, typename TPriorsPrecisionType >
void
MahalanobisDistanceModel< TInputVectorImage, TPriorsPrecisionType >
::EstimateRobust() {

	size_t npix = this->GetInput()->GetLargestPossibleRegion().GetNumberOfPixels();
	size_t ncomps = this->GetInput()->GetNumberOfComponentsPerPixel();

	size_t offset = this->GetPriorsMap()->GetNumberOfComponentsPerPixel();
	size_t nregions = this->m_NumberOfRegions - 1;

	const PriorsPrecisionType* priors = this->GetPriorsMap()->GetBufferPointer();
	const PixelValueType * input = this->GetInput()->GetBufferPointer();


	std::vector<WeightArrayType> weights;
	for( size_t roi = 0; roi < nregions; roi++ ) {
		WeightArrayType warr;
		warr.SetSize(npix);
		warr.Fill(0.0);
		weights.push_back(warr);
	}

	PriorsPrecisionType w;
	for( size_t i = 0; i < npix; i++ ) {
		for( size_t roi = 0; roi < nregions; roi++ ) {
			w = *(priors + offset * i + roi);
			weights[roi][i] = w;
		}
	}

	ReferenceSamplePointer sample = ReferenceSampleType::New();
	sample->SetImage( this->GetInput() );
	for( size_t roi = 0; roi < nregions; roi++ ) {
		InternalFunctionPointer mf = InternalFunctionType::New();

		CovarianceFilterPointer covFilter = CovarianceFilter::New();
		covFilter->SetInput( sample );
		covFilter->SetWeights( weights[roi] );
		covFilter->Update();

		MeasurementVectorType mean = covFilter->GetMean();
		mf->SetMean(mean);

		CovarianceMatrixType cov = covFilter->GetCovarianceMatrix();
		mf->SetCovariance( cov );
		mf->SetRange(covFilter->GetRangeMin(), covFilter->GetRangeMax() );
		mf->Initialize();

		this->m_Memberships[roi] = mf;
		this->m_RegionOffsetContainer[roi] = mf->GetOffsetTerm();

		double maxv = mf->GetMaximumValue() * 1.0e3;
		if( maxv > this->m_MaxEnergy ) {
			this->m_MaxEnergy = maxv;
		}
		this->m_Means[roi] = mean;
		this->m_Covariances[roi] = cov;
	}
}

template< typename TInputVectorImage, typename TPriorsPrecisionType >
typename MahalanobisDistanceModel< TInputVectorImage, TPriorsPrecisionType >::MeasureType
MahalanobisDistanceModel< TInputVectorImage, TPriorsPrecisionType >
::ComputeCovarianceDeterminant(CovarianceMatrixType& cov) const{
	typedef typename CovarianceMatrixType::ValueType ValueType;
	size_t ncomps = cov.Rows();
	CovarianceMatrixType invcov;
	MeasureType det = 1.0;

	if( ncomps > 1 ) {
		// Compute diagonal and check that eigenvectors >= 0.0
		typedef typename vnl_diag_matrix<ValueType>::iterator DiagonalIterator;
		typedef vnl_symmetric_eigensystem<ValueType> Eigensystem;
		vnl_matrix< ValueType > vnlCov = cov.GetVnlMatrix();
		Eigensystem* e = new Eigensystem( vnlCov );

		bool modified = false;
		DiagonalIterator itD = e->D.begin();
		while ( itD!= e->D.end() ) {
			if (*itD < 0) {
				*itD = 0.;
				modified = true;
			}
			itD++;
		}

		if (modified)
			cov = e->recompose();

		delete e;

		// the inverse of the covariance matrix is first computed by SVD
		vnl_matrix_inverse< ValueType > inv_cov( cov.GetVnlMatrix() );

		// the determinant is then costless this way
		det = inv_cov.determinant_magnitude();

		if( det < 0.) {
			itkExceptionMacro( << "| sigma | < 0" );
		}

		// FIXME Singurality Threshold for Covariance matrix: 1e-6 is an arbitrary value!!!
		const ValueType singularThreshold = 1.0e-10;
		if( det > singularThreshold ) {
			// allocate the memory for inverse covariance matrix
			invcov = inv_cov.inverse();
		} else {
			// TODO Perform cholesky diagonalization and select the semi-positive aproximation
			vnl_matrix< double > diag_cov( ncomps, ncomps );
			for ( size_t i = 0; i<ncomps; i++)
				for ( size_t j = 0; j<ncomps; j++)
					diag_cov[i][j] = vnlCov[i][j];
			vnl_ldl_cholesky* chol = new vnl_ldl_cholesky( diag_cov );
			vnl_vector< double > D( chol->diagonal() );
			det = dot_product( D, D );
			vnl_matrix_inverse< double > R (chol->upper_triangle());

			for ( size_t i = 0; i<ncomps; i++)
				for ( size_t j = 0; j<ncomps; j++)
					invcov(i,j) = R.inverse()[i][j];
		}
	} else {
		det = fabs( cov(0,0) );
	}
	return log( det );
}

template< typename TInputVectorImage, typename TPriorsPrecisionType >
std::string
MahalanobisDistanceModel< TInputVectorImage, TPriorsPrecisionType >
::PrintFormattedDescriptors() {
	std::stringstream ss;
	ss << "{ \"descriptors\" : { \"number\": " << this->m_NumberOfRegions << ", \"values\": [";
	size_t nrois = this->m_NumberOfRegions - 1;

	for ( size_t i = 0; i<nrois; i++ ){
		if (i>0) ss<<",";

		ss << "{ \"id\": " << i << ", \"mu\": [";

		for ( size_t l = 0; l<this->m_Means[i].Size(); l++ ) {
			if( l>0 ) ss << ",";
			ss << this->m_Means[i][l];
		}
		ss << "], \"determinant\": " << this->ComputeCovarianceDeterminant(this->m_Covariances[i]);
		ss << ", \"cov\": [ ";

		for( size_t j = 0; j<this->m_Covariances[i].GetVnlMatrix().rows(); j++ ) {
			for( size_t k = 0; k<this->m_Covariances[i].GetVnlMatrix().cols(); k++ ) {
				if( j>0 || k>0 ) ss << ",";
				ss << this->m_Covariances[i](j,k);
			}
		}
		ss << "] }";
	}
	ss << "] } }";

	return ss.str();
}

}


#endif /* _MAHALANOBISDISTANCEMODEL_HXX_ */