#ifndef ACTSDuplicateRemoval_h
#define ACTSDuplicateRemoval_h 1

#include <marlin/Processor.h>

#include <EVENT/Track.h>

//! \brief Remove track duplicates
/**
 * If tracks share more than 50% of hits, then
 * remove the best one.
 *
 * @author Karol Krizka
 * @version $Id$
 */
class ACTSDuplicateRemoval : public marlin::Processor
{
 public:
  virtual marlin::Processor*  newProcessor() { return new ACTSDuplicateRemoval ; }

  ACTSDuplicateRemoval(const ACTSDuplicateRemoval &) = delete ;
  ACTSDuplicateRemoval& operator =(const ACTSDuplicateRemoval &) = delete ;
  ACTSDuplicateRemoval() ;

  /** Called at the begin of the job before anything is read.
   * Use to initialize the processor, e.g. book histograms.
   */
  virtual void init() ;

  /** Called for every run.
   */
  virtual void processRunHeader( LCRunHeader* run ) ;

  /** Called for every event - the working horse.
   */
  virtual void processEvent( LCEvent * evt ) ; 
  
  virtual void check( LCEvent * evt ) ; 

  /** Called after data processing for clean up.
   */
  virtual void end() ;

 private:
  std::string _inputTrackCollection;
  std::string _outputTrackCollection;

};

namespace ACTSTracking
{
//! Compare tracks by quality, best first
/**
 * Decides which track is better using the following algorithm
 *  1. Track with higher number of hits is better
 *  2. Track with smaller chi2 is better
 *
 * 2. is only run with 1. is ambigious.
 */
bool track_duplicate_compare(const EVENT::Track* trk1, const EVENT::Track* trk2);
}

#endif
