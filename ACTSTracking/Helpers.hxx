#pragma once

#include <EVENT/LCEvent.h>
#include <EVENT/Track.h>
#include <EVENT/TrackState.h>

#include <Acts/EventData/TrackParameters.hpp>

#include <Acts/MagneticField/MagneticFieldProvider.hpp>

#include <Acts/TrackFinding/CombinatorialKalmanFilter.hpp>
#include <Acts/TrackFitting/KalmanFitter.hpp>

#include "SourceLink.hxx"

namespace ACTSTracking
{

//! Convert ACTS CKF result to LCIO track class
/**
 * Converted propertie are:
 *  - goodness of fit (chi2, ndf)
 *  - associated hits
 *  - track states at IP
 *
 * \param fitOutput CKF fit result
 * \param trackTip index of track to convert inside fit result
 * \param magneticField magnetic field at different locations in the detector
 * \param magCache cache to help with magnetic field lookup 
 *
 * \return Track with equivalent parameters of the ACTS track
 */
EVENT::Track* ACTS2Marlin_track(const Acts::CombinatorialKalmanFilterResult<ACTSTracking::SourceLink>& fitOutput,
                                std::size_t trackTip,
                                std::shared_ptr<Acts::MagneticFieldProvider> magneticField,
				Acts::MagneticFieldProvider::Cache& magCache);

//! Convert ACTS KF result to LCIO track class
/**
 * Converted propertie are:
 *  - goodness of fit (chi2, ndf)
 *  - associated hits
 *  - track states at IP
 *
 * \param fitOutput KF fit result
 * \param magneticField magnetic field at different locations in the detector
 * \param magCache cache to help with magnetic field lookup 
 *
 * \return Track with equivalent parameters of the ACTS track
 */
EVENT::Track* ACTS2Marlin_track(const Acts::KalmanFitterResult<ACTSTracking::SourceLink>& fitOutput,
                                std::shared_ptr<Acts::MagneticFieldProvider> magneticField,
				Acts::MagneticFieldProvider::Cache& magCache);

//! Convert ACTS track state class to Marlin class
/**
 * \param location Location where the track state is defined (ie: `AtIP`)
 * \param params ACTS track state parameters
 * \params Bz magnetic field at location of track state [Tesla]
 *
 * \return Track state with equivalent parameters of the ACTS track
 */
EVENT::TrackState* ACTS2Marlin_trackState(int location,
                                         const Acts::BoundTrackParameters& params,
                                         double Bz);

EVENT::TrackState* ACTS2Marlin_trackState(int location,
					  const Acts::BoundVector& value, const Acts::BoundMatrix& cov,
					  double Bz);

//! Get collection from `LCEvent` with silent fail
/**
 * \param evt event store
 * \param event collection name
 *
 * \return Collection, if found, `nullptr` otherwise
 */
EVENT::LCCollection* getCollection(EVENT::LCEvent* evt, const std::string& name);
}
