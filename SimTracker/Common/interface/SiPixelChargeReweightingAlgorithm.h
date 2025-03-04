#ifndef SimTracker_Common_SiPixelChargeReweightingAlgorithm_h
#define SimTracker_Common_SiPixelChargeReweightingAlgorithm_h

// Original Author Caroline Collard
// September 2020 : Extraction of the code for cluster charge reweighting from SiPixelDigitizerAlgorithm to a new class

// forward declarations
#include <map>
#include <memory>
#include <vector>
#include <iostream>
#include <iomanip>
#include <type_traits>
#include <gsl/gsl_sf_erf.h>

#include "boost/multi_array.hpp"

#include "CondFormats/DataRecord/interface/SiPixel2DTemplateDBObjectRcd.h"
#include "CondFormats/SiPixelObjects/interface/GlobalPixel.h"
#include "CondFormats/SiPixelObjects/interface/LocalPixel.h"
#include "CondFormats/SiPixelObjects/interface/SiPixel2DTemplateDBObject.h"
#include "CondFormats/SiPixelTransient/interface/SiPixelTemplate2D.h"
#include "DataFormats/DetId/interface/DetId.h"
#include "DataFormats/SiPixelDetId/interface/PixelSubdetector.h"
#include "DataFormats/SiPixelDigi/interface/PixelDigi.h"
#include "DataFormats/TrackerCommon/interface/TrackerTopology.h"
#include "FWCore/Framework/interface/ConsumesCollector.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ServiceRegistry/interface/Service.h"
#include "FWCore/Utilities/interface/Exception.h"
#include "Geometry/CommonDetUnit/interface/PixelGeomDetUnit.h"
#include "Geometry/CommonTopologies/interface/PixelTopology.h"
#include "Geometry/Records/interface/IdealGeometryRecord.h"
#include "Geometry/Records/interface/TrackerDigiGeometryRecord.h"
#include "Geometry/TrackerGeometryBuilder/interface/TrackerGeometry.h"
#include "SimDataFormats/Track/interface/SimTrack.h"
#include "SimDataFormats/TrackerDigiSimLink/interface/PixelDigiSimLink.h"
#include "SimDataFormats/TrackingHit/interface/PSimHitContainer.h"
#include "SimTracker/Common/interface/DigitizerUtility.h"
#include "SimTracker/SiPixelDigitizer/plugins/SiPixelDigitizerAlgorithm.h"

// forward declarations
class DetId;
class GaussianTailNoiseGenerator;
class PixelDigi;
class PixelDigiSimLink;
class PixelGeomDetUnit;
class SiG4UniversalFluctuation;

class SiPixelChargeReweightingAlgorithm {
public:
  SiPixelChargeReweightingAlgorithm(const edm::ParameterSet& conf, edm::ConsumesCollector iC);
  ~SiPixelChargeReweightingAlgorithm();

  // initialization that cannot be done in the constructor
  void init(const edm::EventSetup& es);

  typedef std::map<int, digitizerUtility::Amplitude, std::less<int> > signal_map_type;  // from Digi.Skel.
  typedef signal_map_type::iterator signal_map_iterator;                                // from Digi.Skel.
  typedef signal_map_type::const_iterator signal_map_const_iterator;                    // from Digi.Skel.

  bool hitSignalReweight(const PSimHit& hit,
                         std::map<int, float, std::less<int> >& hit_signal,
                         const size_t hitIndex,
                         const size_t hitIndex4CR,
                         const unsigned int tofBin,
                         const PixelTopology* topol,
                         uint32_t detID,
                         signal_map_type& theSignal,
                         unsigned short int processType,
                         const bool& boolmakeDigiSimLinks);

  bool lateSignalReweight(const PixelGeomDetUnit* pixdet,
                          std::vector<PixelDigi>& digis,
                          PixelSimHitExtraInfo& loopTempSH,
                          signal_map_type& theNewDigiSignal,
                          const TrackerTopology* tTopo,
                          CLHEP::HepRandomEngine* engine);

private:
  // Internal typedef
  typedef std::map<uint32_t, signal_map_type> signalMaps;
  typedef GloballyPositioned<double> Frame;
  typedef std::vector<edm::ParameterSet> Parameters;
  typedef boost::multi_array<float, 2> array_2d;

  std::vector<SiPixelTemplateStore2D> templateStores_;

  // Variables and objects for the charge reweighting using 2D templates
  SiPixelTemplate2D templ2D;
  std::vector<bool> xdouble;
  std::vector<bool> ydouble;
  std::vector<float> track;
  int IDnum, IDden;

  const bool UseReweighting;
  bool applyLateReweighting_;
  const bool PrintClusters;
  const bool PrintTemplates;

  static constexpr float cmToMicrons = 10000.f;

  edm::ESGetToken<SiPixel2DTemplateDBObject, SiPixel2DTemplateDBObjectRcd> SiPixel2DTemp_den_token_;
  edm::ESGetToken<SiPixel2DTemplateDBObject, SiPixel2DTemplateDBObjectRcd> SiPixel2DTemp_num_token_;
  const SiPixel2DTemplateDBObject* dbobject_den;
  const SiPixel2DTemplateDBObject* dbobject_num;

  // methods for charge reweighting in irradiated sensors
  int PixelTempRewgt2D(int id_gen, int id_rewgt, array_2d& cluster);
  void printCluster(array_2d& cluster);
  void printCluster(float arr[BXM2][BYM2]);
  void printCluster(float arr[TXSIZE][TYSIZE]);
};

inline void SiPixelChargeReweightingAlgorithm::init(const edm::EventSetup& es) {
  // Read template files for charge reweighting
  if (UseReweighting || applyLateReweighting_) {
    dbobject_den = &es.getData(SiPixel2DTemp_den_token_);
    dbobject_num = &es.getData(SiPixel2DTemp_num_token_);

    int numOfTemplates = dbobject_den->numOfTempl() + dbobject_num->numOfTempl();
    templateStores_.reserve(numOfTemplates);
    SiPixelTemplate2D::pushfile(*dbobject_den, templateStores_);
    SiPixelTemplate2D::pushfile(*dbobject_num, templateStores_);

    track.resize(6);
  }
}

//=========================================================================

inline SiPixelChargeReweightingAlgorithm::SiPixelChargeReweightingAlgorithm(const edm::ParameterSet& conf,
                                                                            edm::ConsumesCollector iC)
    :

      templ2D(templateStores_),
      xdouble(TXSIZE),
      ydouble(TYSIZE),
      IDnum(conf.exists("TemplateIDnumerator") ? conf.getParameter<int>("TemplateIDnumerator") : 0),
      IDden(conf.exists("TemplateIDdenominator") ? conf.getParameter<int>("TemplateIDdenominator") : 0),

      UseReweighting(conf.getParameter<bool>("UseReweighting")),
      applyLateReweighting_(conf.exists("applyLateReweighting") ? conf.getParameter<bool>("applyLateReweighting")
                                                                : false),
      PrintClusters(conf.getParameter<bool>("PrintClusters")),
      PrintTemplates(conf.getParameter<bool>("PrintTemplates")) {
  if (UseReweighting || applyLateReweighting_) {
    SiPixel2DTemp_den_token_ = iC.esConsumes(edm::ESInputTag("", "denominator"));
    SiPixel2DTemp_num_token_ = iC.esConsumes(edm::ESInputTag("", "numerator"));
  }
  edm::LogVerbatim("PixelDigitizer ") << "SiPixelChargeReweightingAlgorithm constructed"
                                      << " with UseReweighting = " << UseReweighting
                                      << " and applyLateReweighting_ = " << applyLateReweighting_;
}

//=========================================================================
inline SiPixelChargeReweightingAlgorithm::~SiPixelChargeReweightingAlgorithm() {
  LogDebug("PixelDigitizer") << "SiPixelChargeReweightingAlgorithm deleted";
}

//============================================================================

inline bool SiPixelChargeReweightingAlgorithm::hitSignalReweight(const PSimHit& hit,
                                                                 std::map<int, float, std::less<int> >& hit_signal,
                                                                 const size_t hitIndex,
                                                                 const size_t hitIndex4CR,
                                                                 const unsigned int tofBin,
                                                                 const PixelTopology* topol,
                                                                 uint32_t detID,
                                                                 signal_map_type& theSignal,
                                                                 unsigned short int processType,
                                                                 const bool& boolmakeDigiSimLinks) {
  int irow_min = topol->nrows();
  int irow_max = 0;
  int icol_min = topol->ncolumns();
  int icol_max = 0;

  float chargeBefore = 0;
  float chargeAfter = 0;
  signal_map_type hitSignal;
  LocalVector direction = hit.exitPoint() - hit.entryPoint();

  for (std::map<int, float, std::less<int> >::const_iterator im = hit_signal.begin(); im != hit_signal.end(); ++im) {
    int chan = (*im).first;
    std::pair<int, int> pixelWithCharge = PixelDigi::channelToPixel(chan);

    hitSignal[chan] += (boolmakeDigiSimLinks ? digitizerUtility::Amplitude(
                                                   (*im).second, &hit, hitIndex, hitIndex4CR, tofBin, (*im).second)
                                             : digitizerUtility::Amplitude((*im).second, (*im).second));
    chargeBefore += (*im).second;

    if (pixelWithCharge.first < irow_min)
      irow_min = pixelWithCharge.first;
    if (pixelWithCharge.first > irow_max)
      irow_max = pixelWithCharge.first;
    if (pixelWithCharge.second < icol_min)
      icol_min = pixelWithCharge.second;
    if (pixelWithCharge.second > icol_max)
      icol_max = pixelWithCharge.second;
  }

  LocalPoint hitEntryPoint = hit.entryPoint();

  float trajectoryScaleToPosition = std::abs(hitEntryPoint.z() / direction.z());

  LocalPoint hitPosition = hitEntryPoint + trajectoryScaleToPosition * direction;

  MeasurementPoint hitPositionPixel = topol->measurementPosition(hit.localPosition());
  std::pair<int, int> hitPixel =
      std::pair<int, int>(int(floor(hitPositionPixel.x())), int(floor(hitPositionPixel.y())));

  MeasurementPoint originPixel = MeasurementPoint(hitPixel.first - THX + 0.5, hitPixel.second - THY + 0.5);
  LocalPoint origin = topol->localPosition(originPixel);

  MeasurementPoint hitEntryPointPixel = topol->measurementPosition(hit.entryPoint());
  MeasurementPoint hitExitPointPixel = topol->measurementPosition(hit.exitPoint());
  std::pair<int, int> entryPixel =
      std::pair<int, int>(int(floor(hitEntryPointPixel.x())), int(floor(hitEntryPointPixel.y())));
  std::pair<int, int> exitPixel =
      std::pair<int, int>(int(floor(hitExitPointPixel.x())), int(floor(hitExitPointPixel.y())));

  int hitcol_min, hitcol_max, hitrow_min, hitrow_max;
  if (entryPixel.first > exitPixel.first) {
    hitrow_min = exitPixel.first;
    hitrow_max = entryPixel.first;
  } else {
    hitrow_min = entryPixel.first;
    hitrow_max = exitPixel.first;
  }

  if (entryPixel.second > exitPixel.second) {
    hitcol_min = exitPixel.second;
    hitcol_max = entryPixel.second;
  } else {
    hitcol_min = entryPixel.second;
    hitcol_max = exitPixel.second;
  }

#ifdef TP_DEBUG
  LocalPoint CMSSWhitPosition = hit.localPosition();

  LogDebug("Pixel Digitizer") << "\n"
                              << "Particle ID is: " << hit.particleType() << "\n"
                              << "Process type: " << hit.processType() << "\n"
                              << "HitPosition:"
                              << "\n"
                              << "Hit entry x/y/z: " << hit.entryPoint().x() << "  " << hit.entryPoint().y() << "  "
                              << hit.entryPoint().z() << "  "
                              << "Hit exit x/y/z: " << hit.exitPoint().x() << "  " << hit.exitPoint().y() << "  "
                              << hit.exitPoint().z() << "  "

                              << "Pixel Pos - X: " << hitPositionPixel.x() << " Y: " << hitPositionPixel.y() << "\n"
                              << "Cart.Cor. - X: " << CMSSWhitPosition.x() << " Y: " << CMSSWhitPosition.y() << "\n"
                              << "Z=0 Pos - X: " << hitPosition.x() << " Y: " << hitPosition.y() << "\n"

                              << "Origin of the template:"
                              << "\n"
                              << "Pixel Pos - X: " << originPixel.x() << " Y: " << originPixel.y() << "\n"
                              << "Cart.Cor. - X: " << origin.x() << " Y: " << origin.y() << "\n"
                              << "\n"
                              << "Entry/Exit:"
                              << "\n"
                              << "Entry - X: " << hit.entryPoint().x() << " Y: " << hit.entryPoint().y()
                              << " Z: " << hit.entryPoint().z() << "\n"
                              << "Exit - X: " << hit.exitPoint().x() << " Y: " << hit.exitPoint().y()
                              << " Z: " << hit.exitPoint().z() << "\n"

                              << "Entry - X Pixel: " << hitEntryPointPixel.x() << " Y Pixel: " << hitEntryPointPixel.y()
                              << "\n"
                              << "Exit - X Pixel: " << hitExitPointPixel.x() << " Y Pixel: " << hitExitPointPixel.y()
                              << "\n"

                              << "row min: " << irow_min << " col min: " << icol_min << "\n";
#endif

  if (!(irow_min <= hitrow_max && irow_max >= hitrow_min && icol_min <= hitcol_max && icol_max >= hitcol_min)) {
    // The clusters do not have an overlap, hence the hit is NOT reweighted
    return false;
  }

  track[0] = (hitPosition.x() - origin.x()) * cmToMicrons;
  track[1] = (hitPosition.y() - origin.y()) * cmToMicrons;
  track[2] = 0.0f;  //Middle of sensor is origin for Z-axis
  track[3] = direction.x();
  track[4] = direction.y();
  track[5] = direction.z();

  array_2d pixrewgt(boost::extents[TXSIZE][TYSIZE]);

  for (int row = 0; row < TXSIZE; ++row) {
    for (int col = 0; col < TYSIZE; ++col) {
      pixrewgt[row][col] = 0;
    }
  }

  for (int row = 0; row < TXSIZE; ++row) {
    xdouble[row] = topol->isItBigPixelInX(hitPixel.first + row - THX);
  }

  for (int col = 0; col < TYSIZE; ++col) {
    ydouble[col] = topol->isItBigPixelInY(hitPixel.second + col - THY);
  }

  // define loop boundaries that will prevent the row and col loops
  // from going out of physical bounds of the pixel module
  int rowmin = std::max(0, THX - hitPixel.first);
  int rowmax = std::min(TXSIZE, topol->nrows() + THX - hitPixel.first);
  int colmin = std::max(0, THY - hitPixel.second);
  int colmax = std::min(TYSIZE, topol->ncolumns() + THY - hitPixel.second);

  for (int row = rowmin; row < rowmax; ++row) {
    for (int col = colmin; col < colmax; ++col) {
      //Fill charges into 21x13 Pixel Array with hitPixel in centre
      pixrewgt[row][col] =
          hitSignal[PixelDigi::pixelToChannel(hitPixel.first + row - THX, hitPixel.second + col - THY)];
    }
  }

  if (PrintClusters) {
    LogDebug("PixelDigitizer ") << "Cluster before reweighting: ";
    printCluster(pixrewgt);
  }

  int ierr;
  // for unirradiated: 2nd argument is IDden
  // for irradiated: 2nd argument is IDnum
  if (UseReweighting == true) {
    int ID1 = dbobject_num->getTemplateID(detID);
    int ID0 = dbobject_den->getTemplateID(detID);

    if (ID0 == ID1) {
      return false;
    }
    ierr = PixelTempRewgt2D(ID0, ID1, pixrewgt);
  } else {
    ierr = PixelTempRewgt2D(IDden, IDden, pixrewgt);
  }
  if (ierr != 0) {
#ifdef TP_DEBUG
    LogDebug("PixelDigitizer ") << "Cluster Charge Reweighting did not work properly.";
#endif
    return false;
  }

  if (PrintClusters) {
    LogDebug("PixelDigitizer ") << "Cluster after reweighting: ";
    printCluster(pixrewgt);
  }

  for (int row = rowmin; row < rowmax; ++row) {
    for (int col = colmin; col < colmax; ++col) {
      float charge = pixrewgt[row][col];
      if (charge > 0) {
        chargeAfter += charge;
        theSignal[PixelDigi::pixelToChannel(hitPixel.first + row - THX, hitPixel.second + col - THY)] +=
            (boolmakeDigiSimLinks ? digitizerUtility::Amplitude(charge, &hit, hitIndex, hitIndex4CR, tofBin, charge)
                                  : digitizerUtility::Amplitude(charge, charge));
      }
    }
  }

  if (chargeBefore != 0. && chargeAfter == 0.) {
    return false;
  }

  if (PrintClusters) {
    LogDebug("PixelDigitizer ") << "Charges (before->after): " << chargeBefore << " -> " << chargeAfter;
    LogDebug("PixelDigitizer ") << "Charge loss: " << (1 - chargeAfter / chargeBefore) * 100 << " % \n";
  }

  return true;
}

// *******************************************************************************************************
//! Reweight CMSSW clusters to look like clusters corresponding to Pixelav Templates.
//! \param       id_in - (input) identifier of the template corresponding to the input events
//! \param    id_rewgt - (input) identifier of the template corresponding to the output events
//! \param     cluster - (input/output) boost multi_array container of 7x21 array of pixel signals,
//!                       origin of local coords (0,0) at center of pixel cluster[3][10].
//! returns 0 if everything is OK, 1 if angles are outside template coverage (cluster is probably still
//! usable, > 1 if something is wrong (no reweight done).
// *******************************************************************************************************
inline int SiPixelChargeReweightingAlgorithm::PixelTempRewgt2D(int id_in, int id_rewgt, array_2d& cluster) {
  // Local variables
  int i, j, k, l, kclose;
  int nclusx, nclusy, success;
  float xsize, ysize, q50i, q100i, q50r, q10r, q100r, xhit2D, yhit2D, qclust, dist2, dmin2;
  float xy_in[BXM2][BYM2], xy_rewgt[BXM2][BYM2], xy_clust[TXSIZE][TYSIZE];
  int denx_clust[TXSIZE][TYSIZE], deny_clust[TXSIZE][TYSIZE];
  int goodWeightsUsed, nearbyWeightsUsed, noWeightsUsed;
  float cotalpha, cotbeta;
  // success = 0 is returned if everthing is OK
  success = 0;

  // Copy the array to remember original charges
  array_2d clust(cluster);

  // Take the pixel dimensions from the 2D template
  templ2D.getid(id_in);
  xsize = templ2D.xsize();
  ysize = templ2D.ysize();

  // Calculate the track angles

  if (std::abs(track[5]) > 0.f) {
    cotalpha = track[3] / track[5];  //if track[5] (direction in z) is 0 the hit is not processed by re-weighting
    cotbeta = track[4] / track[5];
  } else {
    LogDebug("Pixel Digitizer") << "Reweighting angle is not good! \n";
    return 9;  //returned value here indicates that no reweighting was done in this case
  }

  // The 2-D templates are defined on a shifted coordinate system wrt the 1D templates
  if (ydouble[0]) {
    yhit2D = track[1] - cotbeta * track[2] + ysize;
  } else {
    yhit2D = track[1] - cotbeta * track[2] + 0.5f * ysize;
  }
  if (xdouble[0]) {
    xhit2D = track[0] - cotalpha * track[2] + xsize;
  } else {
    xhit2D = track[0] - cotalpha * track[2] + 0.5f * xsize;
  }

  // Zero the input and output templates
  for (i = 0; i < BYM2; ++i) {
    for (j = 0; j < BXM2; ++j) {
      xy_in[j][i] = 0.f;
      xy_rewgt[j][i] = 0.f;
    }
  }

  // Next, interpolate the CMSSW template needed to analyze this cluster

  if (!templ2D.xytemp(id_in, cotalpha, cotbeta, xhit2D, yhit2D, ydouble, xdouble, xy_in)) {
    success = 1;
  }
  if (success != 0) {
#ifdef TP_DEBUG
    LogDebug("Pixel Digitizer") << "No matching template found \n";
#endif
    return 2;
  }

  if (PrintTemplates) {
    LogDebug("Pixel Digitizer") << "Template unirrad: \n";
    printCluster(xy_in);
  }

  q50i = templ2D.s50();
  //q50i = 0;
  q100i = 2.f * q50i;

  // Check that the cluster container is a 13x21 matrix

  if (cluster.num_dimensions() != 2) {
    edm::LogWarning("Pixel Digitizer") << "Cluster is not 2-dimensional. Return. \n";
    return 3;
  }
  nclusx = (int)cluster.shape()[0];
  nclusy = (int)cluster.shape()[1];
  if (nclusx != TXSIZE || xdouble.size() != TXSIZE) {
    edm::LogWarning("Pixel Digitizer") << "Sizes in x do not match: nclusx=" << nclusx
                                       << "  xdoubleSize=" << xdouble.size() << "  TXSIZE=" << TXSIZE << ". Return. \n";
    return 4;
  }
  if (nclusy != TYSIZE || ydouble.size() != TYSIZE) {
    edm::LogWarning("Pixel Digitizer") << "Sizes in y do not match. Return. \n";
    return 5;
  }

  // Sum initial charge in the cluster

  qclust = 0.f;
  for (i = 0; i < TYSIZE; ++i) {
    for (j = 0; j < TXSIZE; ++j) {
      xy_clust[j][i] = 0.f;
      denx_clust[j][i] = 0;
      deny_clust[j][i] = 0;
      if (cluster[j][i] > q100i) {
        qclust += cluster[j][i];
      }
    }
  }

  // Next, interpolate the physical output template needed to reweight

  if (!templ2D.xytemp(id_rewgt, cotalpha, cotbeta, xhit2D, yhit2D, ydouble, xdouble, xy_rewgt)) {
    success = 1;
  }

  if (PrintTemplates) {
    LogDebug("Pixel Digitizer") << "Template irrad: \n";
    printCluster(xy_rewgt);
  }

  q50r = templ2D.s50();
  q100r = 2.f * q50r;
  q10r = 0.2f * q50r;

  // Find all non-zero denominator pixels in the input template and generate "inside" weights

  int ntpix = 0;
  int ncpix = 0;
  std::vector<int> ytclust;
  std::vector<int> xtclust;
  std::vector<int> ycclust;
  std::vector<int> xcclust;
  qclust = 0.f;
  for (i = 0; i < TYSIZE; ++i) {
    for (j = 0; j < TXSIZE; ++j) {
      if (xy_in[j + 1][i + 1] > q100i) {
        ++ntpix;
        ytclust.push_back(i);
        xtclust.push_back(j);
        xy_clust[j][i] = xy_rewgt[j + 1][i + 1] / xy_in[j + 1][i + 1];
        denx_clust[j][i] = j;
        deny_clust[j][i] = i;
      }
    }
  }

  // Find all non-zero numerator pixels not matched to denominator in the output template and generate "inside" weights

  for (i = 0; i < TYSIZE; ++i) {
    for (j = 0; j < TXSIZE; ++j) {
      if (xy_rewgt[j + 1][i + 1] > q10r && xy_clust[j][i] == 0.f && ntpix > 0) {
        // Search for nearest denominator pixel
        dmin2 = 10000.f;
        kclose = 0;
        for (k = 0; k < ntpix; ++k) {
          dist2 = (i - ytclust[k]) * (i - ytclust[k]) + 0.44444f * (j - xtclust[k]) * (j - xtclust[k]);
          if (dist2 < dmin2) {
            dmin2 = dist2;
            kclose = k;
          }
        }
        xy_clust[j][i] = xy_rewgt[j + 1][i + 1] / xy_in[xtclust[kclose] + 1][ytclust[kclose] + 1];
        denx_clust[j][i] = xtclust[kclose];
        deny_clust[j][i] = ytclust[kclose];
      }
    }
  }

  if (PrintTemplates) {
    LogDebug("Pixel Digitizer") << "Weights: \n";
    printCluster(xy_clust);
  }

  // Do the reweighting
  goodWeightsUsed = 0;
  nearbyWeightsUsed = 0;
  noWeightsUsed = 0;

  for (i = 0; i < TYSIZE; ++i) {
    for (j = 0; j < TXSIZE; ++j) {
      if (xy_clust[j][i] > 0.f) {
        cluster[j][i] = xy_clust[j][i] * clust[denx_clust[j][i]][deny_clust[j][i]];
        if (cluster[j][i] > q100r) {
          qclust += cluster[j][i];
        }
        if (cluster[j][i] > 0) {
          goodWeightsUsed++;
        }
      } else {
        if (clust[j][i] > 0.f) {
          ++ncpix;
          ycclust.push_back(i);
          xcclust.push_back(j);
        }
      }
    }
  }

  // Now reweight pixels outside of template footprint using closest weights

  if (ncpix > 0) {
    for (l = 0; l < ncpix; ++l) {
      i = ycclust[l];
      j = xcclust[l];
      dmin2 = 10000.f;
      kclose = 0;
      for (k = 0; k < ntpix; ++k) {
        dist2 = (i - ytclust[k]) * (i - ytclust[k]) + 0.44444f * (j - xtclust[k]) * (j - xtclust[k]);
        if (dist2 < dmin2) {
          dmin2 = dist2;
          kclose = k;
        }
      }
      if (dmin2 < 5.f) {
        nearbyWeightsUsed++;
        cluster[j][i] *= xy_clust[xtclust[kclose]][ytclust[kclose]];
        if (cluster[j][i] > q100r) {
          qclust += cluster[j][i];
        }
      } else {
        noWeightsUsed++;
        cluster[j][i] = 0.f;
      }
    }
  }

  return success;
}  // PixelTempRewgt2D

inline void SiPixelChargeReweightingAlgorithm::printCluster(array_2d& cluster) {
  for (int col = 0; col < TYSIZE; ++col) {
    for (int row = 0; row < TXSIZE; ++row) {
      LogDebug("Pixel Digitizer") << cluster[row][col];
    }
    LogDebug("Pixel Digitizer") << "\n";
  }
}

inline void SiPixelChargeReweightingAlgorithm::printCluster(float arr[BXM2][BYM2]) {
  for (int col = 0; col < BYM2; ++col) {
    for (int row = 0; row < BXM2; ++row) {
      LogDebug("Pixel Digitizer") << arr[row][col];
    }
    LogDebug("Pixel Digitizer") << "\n";
  }
}

inline void SiPixelChargeReweightingAlgorithm::printCluster(float arr[TXSIZE][TYSIZE]) {
  for (int col = 0; col < TYSIZE; ++col) {
    for (int row = 0; row < TXSIZE; ++row) {
      LogDebug("Pixel Digitizer") << arr[row][col];
    }
    LogDebug("Pixel Digitizer") << "\n";
  }
}

inline bool SiPixelChargeReweightingAlgorithm::lateSignalReweight(const PixelGeomDetUnit* pixdet,
                                                                  std::vector<PixelDigi>& digis,
                                                                  PixelSimHitExtraInfo& loopTempSH,
                                                                  signal_map_type& theNewDigiSignal,
                                                                  const TrackerTopology* tTopo,
                                                                  CLHEP::HepRandomEngine* engine) {
  uint32_t detID = pixdet->geographicalId().rawId();
  const PixelTopology* topol = &pixdet->specificTopology();

  if (UseReweighting) {
    edm::LogError("Pixel Digitizer") << " ******************************** \n";
    edm::LogError("Pixel Digitizer") << " ******************************** \n";
    edm::LogError("Pixel Digitizer") << " ******************************** \n";
    edm::LogError("Pixel Digitizer") << " *****  INCONSISTENCY !!!   ***** \n";
    edm::LogError("Pixel Digitizer")
        << " applyLateReweighting_ and UseReweighting can not be true at the same time for PU ! \n";
    edm::LogError("Pixel Digitizer") << " ---> DO NOT APPLY CHARGE REWEIGHTING TWICE !!! \n";
    edm::LogError("Pixel Digitizer") << " ******************************** \n";
    edm::LogError("Pixel Digitizer") << " ******************************** \n";
    return false;
  }

  signal_map_type theDigiSignal;

  int irow_min = topol->nrows();
  int irow_max = 0;
  int icol_min = topol->ncolumns();
  int icol_max = 0;

  float chargeBefore = 0;
  float chargeAfter = 0;

  //loop on digis
  std::vector<PixelDigi>::const_iterator loopDigi;
  for (loopDigi = digis.begin(); loopDigi != digis.end(); ++loopDigi) {
    unsigned int chan = loopDigi->channel();
    if (loopTempSH.isInTheList(chan)) {
      float corresponding_charge = loopDigi->adc();
      theDigiSignal[chan] += digitizerUtility::Amplitude(corresponding_charge, corresponding_charge);
      chargeBefore += corresponding_charge;
      if (loopDigi->row() < irow_min)
        irow_min = loopDigi->row();
      if (loopDigi->row() > irow_max)
        irow_max = loopDigi->row();
      if (loopDigi->column() < icol_min)
        icol_min = loopDigi->column();
      if (loopDigi->column() > icol_max)
        icol_max = loopDigi->column();
    }
  }
  // end loop on digis

  LocalVector direction = loopTempSH.exitPoint() - loopTempSH.entryPoint();
  LocalPoint hitEntryPoint = loopTempSH.entryPoint();
  float trajectoryScaleToPosition = std::abs(hitEntryPoint.z() / direction.z());
  LocalPoint hitPosition = hitEntryPoint + trajectoryScaleToPosition * direction;

  // addition as now the hit himself is not available
  // https://github.com/cms-sw/cmssw/blob/master/SimDataFormats/TrackingHit/interface/PSimHit.h#L52
  LocalPoint hitLocalPosition = hitEntryPoint + 0.5 * direction;
  MeasurementPoint hitPositionPixel = topol->measurementPosition(hitLocalPosition);
  std::pair<int, int> hitPixel =
      std::pair<int, int>(int(floor(hitPositionPixel.x())), int(floor(hitPositionPixel.y())));

  MeasurementPoint originPixel = MeasurementPoint(hitPixel.first - THX + 0.5, hitPixel.second - THY + 0.5);
  LocalPoint origin = topol->localPosition(originPixel);

  MeasurementPoint hitEntryPointPixel = topol->measurementPosition(loopTempSH.entryPoint());
  MeasurementPoint hitExitPointPixel = topol->measurementPosition(loopTempSH.exitPoint());
  std::pair<int, int> entryPixel =
      std::pair<int, int>(int(floor(hitEntryPointPixel.x())), int(floor(hitEntryPointPixel.y())));
  std::pair<int, int> exitPixel =
      std::pair<int, int>(int(floor(hitExitPointPixel.x())), int(floor(hitExitPointPixel.y())));

  int hitcol_min, hitcol_max, hitrow_min, hitrow_max;
  if (entryPixel.first > exitPixel.first) {
    hitrow_min = exitPixel.first;
    hitrow_max = entryPixel.first;
  } else {
    hitrow_min = entryPixel.first;
    hitrow_max = exitPixel.first;
  }

  if (entryPixel.second > exitPixel.second) {
    hitcol_min = exitPixel.second;
    hitcol_max = entryPixel.second;
  } else {
    hitcol_min = entryPixel.second;
    hitcol_max = exitPixel.second;
  }

  if (!(irow_min <= hitrow_max && irow_max >= hitrow_min && icol_min <= hitcol_max && icol_max >= hitcol_min)) {
    // The clusters do not have an overlap, hence the hit is NOT reweighted
    return false;
  }

  track[0] = (hitPosition.x() - origin.x()) * cmToMicrons;
  track[1] = (hitPosition.y() - origin.y()) * cmToMicrons;
  track[2] = 0.0f;  //Middle of sensor is origin for Z-axis
  track[3] = direction.x();
  track[4] = direction.y();
  track[5] = direction.z();

  array_2d pixrewgt(boost::extents[TXSIZE][TYSIZE]);

  /*
  for (int row = 0; row < TXSIZE; ++row) {
    for (int col = 0; col < TYSIZE; ++col) {
      pixrewgt[row][col] = 0;
    }
  }
*/

  for (int row = 0; row < TXSIZE; ++row) {
    xdouble[row] = topol->isItBigPixelInX(hitPixel.first + row - THX);
  }

  for (int col = 0; col < TYSIZE; ++col) {
    ydouble[col] = topol->isItBigPixelInY(hitPixel.second + col - THY);
  }

  for (int row = 0; row < TXSIZE; ++row) {
    for (int col = 0; col < TYSIZE; ++col) {
      //Fill charges into 21x13 Pixel Array with hitPixel in centre
      pixrewgt[row][col] =
          theDigiSignal[PixelDigi::pixelToChannel(hitPixel.first + row - THX, hitPixel.second + col - THY)];
    }
  }

  if (PrintClusters) {
    LogDebug("Pixel Digitizer") << " Cluster before reweighting: ";
    printCluster(pixrewgt);
  }

  int ierr;
  // for unirradiated: 2nd argument is IDden
  // for irradiated: 2nd argument is IDnum
  int ID1 = dbobject_num->getTemplateID(detID);
  int ID0 = dbobject_den->getTemplateID(detID);

  if (ID0 == ID1) {
    LogDebug("Pixel Digitizer") << " same template for num and den ";
    return false;
  }
  ierr = PixelTempRewgt2D(ID0, ID1, pixrewgt);
  if (ierr != 0) {
#ifdef TP_DEBUG
    LogDebug("PixelDigitizer ") << "Cluster Charge Reweighting did not work properly.";
#endif
    return false;
  }
  if (PrintClusters) {
    LogDebug("Pixel Digitizer") << " Cluster after reweighting: ";
    printCluster(pixrewgt);
  }

  for (int row = 0; row < TXSIZE; ++row) {
    for (int col = 0; col < TYSIZE; ++col) {
      float charge = 0;
      charge = pixrewgt[row][col];
      if ((hitPixel.first + row - THX) >= 0 && (hitPixel.first + row - THX) < topol->nrows() &&
          (hitPixel.second + col - THY) >= 0 && (hitPixel.second + col - THY) < topol->ncolumns() && charge > 0) {
        chargeAfter += charge;
        theNewDigiSignal[PixelDigi::pixelToChannel(hitPixel.first + row - THX, hitPixel.second + col - THY)] +=
            digitizerUtility::Amplitude(charge, charge);
      }
    }
  }

  if (chargeBefore != 0. && chargeAfter == 0.) {
    return false;
  }

  if (PrintClusters) {
    LogDebug("Pixel Digitizer") << "Charges (before->after): " << chargeBefore << " -> " << chargeAfter;
    LogDebug("Pixel Digitizer") << "Charge loss: " << (1 - chargeAfter / chargeBefore) * 100 << " %";
  }

  // need to store the digi out of the 21x13 box.
  for (signal_map_const_iterator i = theDigiSignal.begin(); i != theDigiSignal.end(); ++i) {
    // check if in the 21x13 box
    int chanDigi = (*i).first;
    std::pair<int, int> ip = PixelDigi::channelToPixel(chanDigi);
    int row_digi = ip.first;
    int col_digi = ip.second;
    int i_transformed_row = row_digi - hitPixel.first + THX;
    int i_transformed_col = col_digi - hitPixel.second + THY;
    if (i_transformed_row < 0 || i_transformed_row > TXSIZE || i_transformed_col < 0 || i_transformed_col > TYSIZE) {
      // not in the box
      if (chanDigi >= 0 && (*i).second > 0) {
        theNewDigiSignal[chanDigi] += digitizerUtility::Amplitude((*i).second, (*i).second);
      }
    }
  }

  return true;
}

#endif
