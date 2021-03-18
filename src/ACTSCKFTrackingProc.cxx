#include "ACTSCKFTrackingProc.hxx"

#include <IMPL/LCCollectionVec.h>
#include <IMPL/LCFlagImpl.h>
#include <IMPL/TrackerHitPlaneImpl.h>

#include <EVENT/MCParticle.h>
#include <EVENT/SimTrackerHit.h>

#include <UTIL/LCRelationNavigator.h>
#include <UTIL/LCTrackerConf.h>

#include <IMPL/TrackImpl.h>
#include <IMPL/TrackStateImpl.h>
#include <IMPL/LCRelationImpl.h>

#include <Acts/EventData/MultiTrajectory.hpp>

#include <Acts/Propagator/Navigator.hpp>
#include <Acts/Propagator/Propagator.hpp>
#include <Acts/Propagator/EigenStepper.hpp>

#include <Acts/Surfaces/PerigeeSurface.hpp>

#include <Acts/TrackFinding/CombinatorialKalmanFilter.hpp>
#include <Acts/TrackFinding/MeasurementSelector.hpp>

#include <Acts/TrackFitting/GainMatrixSmoother.hpp>
#include <Acts/TrackFitting/GainMatrixUpdater.hpp>

using namespace Acts::UnitLiterals;

#include "MeasurementCalibrator.hxx"
#include "SourceLink.hxx"

using TrackFinderOptions =
    Acts::CombinatorialKalmanFilterOptions<ACTSTracking::MeasurementCalibrator,
                                           Acts::MeasurementSelector>;

using TrackFinderResult =
    Acts::Result<Acts::CombinatorialKalmanFilterResult<ACTSTracking::SourceLink>>;

using TrackFinderResultContainer =
    std::vector<TrackFinderResult>;

ACTSCKFTrackingProc aACTSCKFTrackingProc;

ACTSCKFTrackingProc::ACTSCKFTrackingProc() : ACTSProcBase("ACTSCKFTrackingProc")
{
  // modify processor description
  _description = "Build and fit tracks out of all hits associated to an MC particle" ;

  // Input collections - mc particles, tracker hits and the relationships between them
  registerInputCollections( LCIO::TRACKERHITPLANE,
                            "TrackerHitCollectionNames" ,
                            "Name of the TrackerHit input collections",
                            _inputTrackerHitCollections ,
                            {} ) ;

  registerInputCollection( LCIO::MCPARTICLE,
                           "MCParticleCollectionName",
                           "Name of the MCParticle input collection",
                           _inputParticleCollection,
                           std::string("MCParticle"));

  // Output collections - tracks and relations
  registerOutputCollection( LCIO::TRACK,
                            "TrackCollectionName",
                            "Name of track output collection",
                            _outputTrackCollection,
                            std::string("TruthTracks"));
}

void ACTSCKFTrackingProc::init()
{
  ACTSProcBase::init();
	
  // Reset counters
  _runNumber = 0 ;
  _eventNumber = 0 ;
  _fitFails = 0;

  //Initialize CellID encoder
  _encoder = std::make_shared<UTIL::BitField64>(lcio::LCTrackerCellID::encoding_string());
}


void ACTSCKFTrackingProc::processRunHeader( LCRunHeader* )
{
  _runNumber++ ;
}

void ACTSCKFTrackingProc::processEvent( LCEvent* evt )
{
  // Construct a perigee surface as the target surface
  std::shared_ptr<Acts::PerigeeSurface> perigeeSurface = Acts::Surface::makeShared<Acts::PerigeeSurface>(
      Acts::Vector3{0., 0., 0.});

  //
  // Make seeds using truth particles
  std::vector<Acts::BoundTrackParameters> seeds;

  // Get the collection of MC particles
  LCCollection* particleCollection = getCollection(_inputParticleCollection, evt);
  if(particleCollection == nullptr) return;

  for(uint32_t idxP=0; idxP<particleCollection->getNumberOfElements(); ++idxP)
  {
    const MCParticle* mcParticle = static_cast<const MCParticle*>(particleCollection->getElementAt(idxP));

    // Tracks are made by stable charged particles from generation
    if(mcParticle->isCreatedInSimulation() ||
       mcParticle->getGeneratorStatus()!=1 ||
       mcParticle->getCharge()==0)
      continue;

    // Create initial parameters
    double px=mcParticle->getMomentum()[0];
    double py=mcParticle->getMomentum()[1];
    double pz=mcParticle->getMomentum()[2];
    double pt=sqrt(px*px+py*py);
    double p =sqrt(px*px+py*py+pz*pz);

    Acts::BoundVector params = Acts::BoundVector::Zero();
    // position/time
    params[Acts::eBoundLoc0  ] = 0;
    params[Acts::eBoundLoc1  ] = 0;
    params[Acts::eBoundTime  ] = mcParticle->getTime();
    // direction angles phi,theta
    params[Acts::eBoundPhi   ] = atan2(py,px);
    params[Acts::eBoundTheta ] = atan2(pt,pz);
    // absolute momentum vector
    params[Acts::eBoundQOverP] = mcParticle->getCharge()/p;

    // build the track covariance matrix using the smearing sigmas 
    Acts::BoundSymMatrix cov = Acts::BoundSymMatrix::Zero();
    cov(Acts::eBoundLoc0  , Acts::eBoundLoc0  ) = std::pow(_initialTrackError_d0              ,2);
    cov(Acts::eBoundLoc1  , Acts::eBoundLoc1  ) = std::pow(_initialTrackError_z0              ,2);
    cov(Acts::eBoundTime  , Acts::eBoundTime  ) = std::pow(1_ns                               ,2);
    cov(Acts::eBoundPhi   , Acts::eBoundPhi   ) = std::pow(_initialTrackError_phi             ,2);
    cov(Acts::eBoundTheta , Acts::eBoundTheta ) = std::pow(_initialTrackError_lambda          ,2);
    cov(Acts::eBoundQOverP, Acts::eBoundQOverP) = std::pow(_initialTrackError_relP * p /(p*p), 2);

    Acts::BoundTrackParameters seed(perigeeSurface, params, mcParticle->getCharge(), cov);
    seeds.push_back(seed);
  }

  //
  // Make a list of measurements
  std::vector<ACTSTracking::SourceLink> sourceLinks;
  ACTSTracking::MeasurementContainer measurements;

  // Loop over each hit collections and get the data
  for(const std::string& collection : _inputTrackerHitCollections)
  {
    // Get the collection of tracker hits
    LCCollection* trackerHitCollection = getCollection(collection, evt);
    if(trackerHitCollection == nullptr) continue;

    // Loop over all hits
    for(uint32_t idxHit=0; idxHit<trackerHitCollection->getNumberOfElements(); idxHit++)
    {
      const EVENT::TrackerHit* hit = static_cast<const EVENT::TrackerHit*>(trackerHitCollection->getElementAt(idxHit));

      // Convert to Acts hit
      const Acts::Surface* surface=findSurface(hit);

      const double* globalpos=hit->getPosition();
      Acts::Result<Acts::Vector2> lpResult = surface->globalToLocal(geometryContext(),
                                                                    {globalpos[0], globalpos[1], globalpos[2]},
                                                                    {0,0,0},
                                                                    0.5_um);
      if(!lpResult.ok())
        throw std::runtime_error("Global to local transformation did not succeed.");

      Acts::Vector2 loc = lpResult.value();

      Acts::SymMatrix2 cov = Acts::SymMatrix2::Zero();
      const EVENT::TrackerHitPlane* hitplane=dynamic_cast<const EVENT::TrackerHitPlane*>(hit);
      if(hitplane)
      {
        cov(0, 0) = std::pow(hitplane->getdU()*Acts::UnitConstants::mm, 2);
        cov(1, 1) = std::pow(hitplane->getdV()*Acts::UnitConstants::mm, 2);
      }
      else
      { throw std::runtime_error("Currently only support TrackerHitPlane."); }

      ACTSTracking::SourceLink sourceLink(surface->geometryId(), measurements.size());
      ACTSTracking::Measurement meas =
          Acts::makeMeasurement(sourceLink, loc, cov, Acts::eBoundLoc0,
                                Acts::eBoundLoc1);

      measurements.push_back(meas);
      sourceLinks .push_back(sourceLink);
    }
  }

  //
  // Initialize track finder
  using Updater = Acts::GainMatrixUpdater;
  using Smoother = Acts::GainMatrixSmoother;
  using Stepper = Acts::EigenStepper<>;
  using Navigator = Acts::Navigator;
  using Propagator = Acts::Propagator<Stepper, Navigator>;
  using CKF =
      Acts::CombinatorialKalmanFilter<Propagator, Updater, Smoother>;

  // construct all components for the fitter
  Stepper stepper(magneticField());
  Navigator navigator(trackingGeometry());
  navigator.resolvePassive = false;
  navigator.resolveMaterial = true;
  navigator.resolveSensitive = true;
  Propagator propagator(std::move(stepper), std::move(navigator));
  CKF trackFinder(std::move(propagator));

  // Set the options
  Acts::MeasurementSelector::Config measurementSelectorCfg={{Acts::GeometryIdentifier(), {15,10}}};

  Acts::PropagatorPlainOptions pOptions;
  pOptions.maxSteps = 10000;
  
  //std::unique_ptr<const Acts::Logger> logger=Acts::getDefaultLogger("TrackFitting", Acts::Logging::Level::VERBOSE);
  TrackFinderOptions ckfOptions
      =TrackFinderOptions(
          geometryContext(), magneticFieldContext(), calibrationContext(),
          ACTSTracking::MeasurementCalibrator(std::move(measurements)),
          Acts::MeasurementSelector(measurementSelectorCfg),
          //Acts::LoggerWrapper{*logger}, pOptions,
          Acts::getDummyLogger(), pOptions,
          &(*perigeeSurface));

  //
  // Output

  // Make the output track collection
  LCCollectionVec* trackCollection = new LCCollectionVec( LCIO::TRACK )  ;
	
  // Enable the track collection to point back to hits
  LCFlagImpl trkFlag(0) ;
  trkFlag.setBit( LCIO::TRBIT_HITS ) ;
  trackCollection->setFlag( trkFlag.getFlag()  ) ;

  //
  // Find the tracks
  TrackFinderResultContainer results=trackFinder.findTracks(sourceLinks, seeds, ckfOptions);
  for (TrackFinderResult& result : results)
  {
    if (result.ok())
    {
      const Acts::CombinatorialKalmanFilterResult<ACTSTracking::SourceLink>& fitOutput = result.value();
      for(const size_t& trackTip : fitOutput.trackTips)
      {
        if(fitOutput.fittedParameters.count(trackTip)==0)
        {
          streamlog_out(WARNING) << "No fitted track parameters for trajectory with entry index = " << trackTip << std::endl;
          continue;
        }

        const Acts::BoundTrackParameters& params = fitOutput.fittedParameters.at(trackTip);
        streamlog_out(DEBUG) << "Fitted Paramemeters" << std::endl << params << std::endl;

        // Make the track object and relations object
        IMPL::TrackImpl* track = new IMPL::TrackImpl ;

        Acts::MultiTrajectoryHelpers::TrajectoryState trajState =
            Acts::MultiTrajectoryHelpers::trajectoryState(fitOutput.fittedStates, trackTip);
        track->setChi2(trajState.chi2Sum);
        track->setNdf (trajState.NDF    );

        // TODO: Add hits on track
  
        //
        // AtIP: Overall fit results as fittedParameters

        IMPL::TrackStateImpl* trackStateAtIP = new IMPL::TrackStateImpl();
        trackStateAtIP->setLocation(lcio::TrackState::AtIP);
        track->trackStates().push_back(trackStateAtIP);

        // Fill the parameters
        static const Acts::Vector3 zeropos(0,0,0);

        double d0    =params.parameters()[Acts::eBoundLoc0  ];
        double z0    =params.parameters()[Acts::eBoundLoc1  ];
        double phi   =params.parameters()[Acts::eBoundPhi   ];
        double theta =params.parameters()[Acts::eBoundTheta ];
        double qoverp=params.parameters()[Acts::eBoundQOverP];

        double p=1e3/qoverp;
        double Bz=magneticField()->getField(zeropos)[2]/Acts::UnitConstants::T;
        double omega=(0.3*Bz)/(p*std::sin(theta));
        double tanlambda=std::tan(theta);

        trackStateAtIP->setPhi      (phi);
        trackStateAtIP->setTanLambda(tanlambda);
        trackStateAtIP->setOmega    (omega);
        trackStateAtIP->setD0       (d0);
        trackStateAtIP->setZ0       (z0);

        // Fill the covariance matrix
        //d0, phi, omega, z0, tan(lambda)
        Acts::BoundTrackParameters::CovarianceMatrix cov=params.covariance().value();

        double var_d0    =cov(Acts::eBoundLoc0  , Acts::eBoundLoc0  );
        double var_z0    =cov(Acts::eBoundLoc1  , Acts::eBoundLoc1  );
        double var_phi   =cov(Acts::eBoundPhi   , Acts::eBoundPhi   );
        double var_theta =cov(Acts::eBoundTheta , Acts::eBoundTheta );
        double var_qoverp=cov(Acts::eBoundQOverP, Acts::eBoundQOverP);

        double var_omega    =
            var_qoverp*std::pow(omega/(qoverp*1e-3)      , 2) +
            var_theta *std::pow(omega/std::tan(var_theta), 2);
        double var_tanlambda=var_theta*std::pow(1/std::cos(theta), 4);

        FloatVec lcioCov(15, 0);
        lcioCov[ 0]=var_d0;
        lcioCov[ 2]=var_phi;
        lcioCov[ 5]=var_omega;
        lcioCov[ 9]=var_z0;
        lcioCov[14]=var_tanlambda;
        // TODO: Add off-diagonals

        trackStateAtIP->setCovMatrix(lcioCov);

        //
        // Save results
        trackCollection->addElement(track);

      // // Can be used get track states at different layers
      // for(size_t trackTip : fitOutput.trackTips)
      // {
      //   std::cout << "trackTip " << trackTip << std::endl;
      //   fitOutput.fittedStates.visitBackwards(trackTip, [](Acts::MultiTrajectory<ACTSTracking::SourceLink>::ConstTrackStateProxy state)
      //   {
      //     const Acts::TrackStateType& typeFlags = state.typeFlags();
      //     if(!typeFlags.test(Acts::TrackStateFlag::MeasurementFlag))
      //       return true;

      //     const Acts::Surface& surface = state.referenceSurface();

      //     const Acts::GeometryIdentifier& geoID = surface.geometryId();
      //     std::cout << "volume = " << geoID.volume() << std::endl;
      //     std::cout << "layer = " << geoID.layer() << std::endl;
      //     std::cout << "sensitive = " << geoID.sensitive() << std::endl;

      //     const Acts::BoundVector& params=state.smoothed();
      //     std::cout << params[Acts::eBoundQOverP] << std::endl;
      //     return true;
      //   });
      // }
      }
    }
    else
    {
      streamlog_out(WARNING) << "Track fit error: " << result.error() << std::endl;
      _fitFails++;
    }
  }

  // Save the output track collection
  evt->addCollection( trackCollection , _outputTrackCollection ) ;
  
  // Increment the event number
  _eventNumber++ ;
}

void ACTSCKFTrackingProc::check( LCEvent* )
{
  // nothing to check here - could be used to fill checkplots in reconstruction processor
}


void ACTSCKFTrackingProc::end()
{
  streamlog_out(MESSAGE) << " end()  " << name()
                         << " processed " << _eventNumber << " events in " << _runNumber << " runs "
                         << std::endl ;
}

LCCollection* ACTSCKFTrackingProc::getCollection(const std::string& collectionName, LCEvent* evt)
{
  try
  {
    return evt->getCollection( collectionName );
  }
  catch(DataNotAvailableException &e)
  {
    streamlog_out( DEBUG5 ) << "- cannot get collection. Collection " << collectionName << " is unavailable" << std::endl;
    return nullptr;
  }
}

int ACTSCKFTrackingProc::getSubdetector(const lcio::TrackerHit* hit)
{ _encoder->setValue(hit->getCellID0()); return (*_encoder)[lcio::LCTrackerCellID::subdet()]; }

int ACTSCKFTrackingProc::getLayer(const lcio::TrackerHit* hit)
{ _encoder->setValue(hit->getCellID0()); return (*_encoder)[lcio::LCTrackerCellID::layer ()]; }

void ACTSCKFTrackingProc::removeHitsSameLayer(const std::vector<TrackerHit*> &trackHits, std::vector<TrackerHit*> &trackFilteredHits)
{
  trackFilteredHits.push_back(*(trackHits.begin()));

  for(std::vector<TrackerHit*>::const_iterator it = trackHits.begin()+1; it != trackHits.end(); ++it)
  {
    int subdet = getSubdetector(*it);
    int layer = getLayer(*it);
    if( subdet != getSubdetector(*(it-1)) )
    {
      trackFilteredHits.push_back(*it);
    }
    else if( layer != getLayer(*(it-1)) )
    {
      trackFilteredHits.push_back(*it);
    }
  }
}
