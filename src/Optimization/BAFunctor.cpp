#include "BAFunctor.h"

#include <iostream>

BAFunctor::BAFunctor(const Eigen::Index numPoints, const Eigen::Index numCameras, const Matrix2XX &measurements,
	const std::vector<int> &correspondingView, const std::vector<int> &correspondingPoint,
	Scalar inlierThreshold) 
	: Base(numPoints * 3 + numCameras * 9, measurements.cols() * 2),
	measurements(measurements),
	correspondingView(correspondingView),
	correspondingPoint(correspondingPoint),
	inlierThreshold(inlierThreshold),
	numParameters(numPoints * 3 + numCameras * 9),
	numPointParams(numPoints * 3),
	numResiduals(measurements.cols() * 2),
	numJacobianNonzeros(measurements.cols() * 2 * 3 + 9 * numCameras) {

	initWorkspace();
}

void BAFunctor::initWorkspace() {

}

Scalar BAFunctor::estimateNorm(InputType const& x, StepType const& diag) {
	Index pt_base = 0;
	Index cam_base = x.nDataPoints() * 3;

	// Camera parameters ordering
	Index numPointCoords = 3;	// xyz
	Index numCamParams = 9;
	Index translationOffset = 0;
	Index rotationOffset = 3;
	Index focalLengthOffset = 6;
	Index radialParamsOffset = 7;

	Scalar total = 0.0;
	Vector3X T, omega;
	Vector2X k12;
	Scalar focalLength = 0.0;
	Matrix3X R;
	for (int i = 0; i < x.nCameras(); i++) {
		T = x.cams[i].getTranslation();
		R = x.cams[i].getRotation();
		Math::createRodriguesParamFromRotationMatrix(R, omega);
		k12 = Vector2X(x.distortions[i].getK1(), x.distortions[i].getK2());
		focalLength = x.cams[i].getFocalLength();

		total += T.cwiseProduct(diag.segment<3>(cam_base + i * numCamParams)).stableNorm();
		total += omega.cwiseProduct(diag.segment<3>(cam_base + i * numCamParams + rotationOffset)).stableNorm();
		total += k12.cwiseProduct(diag.segment<2>(cam_base + i * numCamParams + radialParamsOffset)).stableNorm();
		total += sqrt((focalLength * diag[cam_base + i * numCamParams + focalLengthOffset]) * (focalLength * diag[cam_base + i * numCamParams + focalLengthOffset]));
	}
	total = total * total;


	Map<VectorXX> xtop{ (Scalar*)x.data_points.data(), x.nDataPoints() * 3 };
	total += xtop.cwiseProduct(diag.head(x.nDataPoints() * 3)).squaredNorm();

	return Scalar(sqrt(total));
}

// And tell the algorithm how to set the QR parameters.
void BAFunctor::initQRSolver(SchurlikeQRSolver &qr) {
#ifdef QRKIT
  qr.setSparseBlockParams(this->measurements.cols() * 2 + this->numPointParams, this->numPointParams);
#elif QRCHOL
  qr.setSparseBlockParams(this->measurements.cols() * 2 + this->numPointParams, this->numPointParams);
#elif MOREQR
  qr.setSparseBlockParams(this->measurements.cols() * 2, this->numPointParams);
#endif
}

void BAFunctor::initQRSolverInner(SchurlikeQRSolver &qr) {
#ifdef MOREQR 
  qr.setSparseBlockParams(this->numPointParams * 2, this->numPointParams);
#endif
}

// Functor functions
// 1. Evaluate the residuals at x
int BAFunctor::operator()(const InputType& x, ValueType& fvec) {
	this->f_impl(x, fvec);

	return 0;
}

// 2. Evaluate jacobian at x
int BAFunctor::df(const InputType& x, JacobianType& fjac) {
	// Fill Jacobian columns.  
	Eigen::TripletArray<Scalar, typename JacobianType::Index> jvals(this->numJacobianNonzeros);

	this->df_impl(x, jvals);

	fjac.resize(this->numResiduals, this->numParameters);
	// Do not redefine the functor treating duplicate entries!!! The implementation expects to sum them up as done by default.
	fjac.setFromTriplets(jvals.begin(), jvals.end());
	fjac.makeCompressed();

	return 0;
}

void BAFunctor::increment_in_place(InputType* x, StepType const& p) {


	this->increment_in_place_impl(x, p);
}

// Functor functions
// 1. Evaluate the residuals at x
void BAFunctor::f_impl(const InputType& x, ValueType& fvec) {
	this->E_pos(x, this->measurements, this->correspondingView, this->correspondingPoint, fvec);
}

// 2. Evaluate jacobian at x
void BAFunctor::df_impl(const InputType& x, Eigen::TripletArray<Scalar, typename JacobianType::Index>& jvals) {
	this->dE_pos(x, jvals);
}

void BAFunctor::increment_in_place_impl(InputType* x, StepType const& p) {
	// The parameters are passed to the optimization as:
	this->update_params(x, p);
}