/******************************************************************************
 * Revision 1.3  2012/04/25 05:05:08  hmasui
 * Fix the multiplicity calculation in the analysis maker, use multiplicity from tree. Added higher harmonics. Unit weight for multiplicity related quantities. Use STAR logger.
 *
 * Revision 1.2  2010/11/21 02:27:56  hmasui
 * Add re-weighting. Calculate multiplicity from Npart and Ncoll. Use STAR logger
 *
******************************************************************************/

#include <assert.h>
#include <fstream>

#include "TError.h"
#include "TFile.h"
#include "TSystem.h"
#include "TTree.h"
#include "TSystem.h"

#include "../CentralityMaker/Centrality.h"
#include "../CentralityMaker/CentralityMaker.h"
#include "../CentralityMaker/NegativeBinomial.h"
#include "GlauberConstUtilities.h"
#include "GlauberHistogramMaker.h"
#include "GlauberCumulantHistogramMaker.h"
#include "../GlauberTree/GlauberTree.h"
#include "../GlauberUtilities/GlauberUtilities.h"

#include "GlauberAnalysisMaker.h"

using std::fstream ;
using std::map ;
using std::pair ;


const TString GlauberAnalysisMaker::mTypeName[] = {
    "default", "small", "large", "smallXsec", "largeXsec", "gauss",
    "smallNpp", "largeNpp", "smallTotal", "largeTotal",
    "lowrw", "highrw"
  };

  const TString GlauberAnalysisMaker::mDescription[] = {
    "default",
    "small R, large d",
    "large R, small d",
    "small #sigma_{NN}",
    "large #sigma_{NN}",
//    "gray disk overlap",
    "gaussian overlap",
    "small n_{pp}, large x",
    "large n_{pp}, small x",
    "-5\% total cross section",
    "+5\% total cross section",
    "+2(-2) sigma p0 (p1) parameter for re-weighting",
    "-2(+2) sigma p0 (p1) parameter for re-weighting"
  };

//  const UInt_t StGlauberAnalysisMaker::mCentralityId[] = {
//    0,   // "default",
//    0,   // "small R, large d",
//    0,   // "large R, small d",
//    0,   // "small #sigma_{NN}",
//    0,   // "large #sigma_{NN}",
//    0,   // "gray disk overlap",
//    0,   // "gaussian overlap",
//    1,   // "small n_{pp}, large x",
//    2,   // "large n_{pp}, small x",
//    0,   // "-5% total cross section",
//    0    // "+5% total cross section"
//  };

//____________________________________________________________________________________________________
// Default constructor
GlauberAnalysisMaker::GlauberAnalysisMaker(const TString type, const TString system, const TString outputFileName,
    const TString tableDir)
  : mType(type), mOutputFile(0), mOutputFileName(outputFileName),
  mUnitWeight(kFALSE), // Unit weight flag (default is false, use multiplicity weight)
  mReweighting(kFALSE) // Re-weighting flag (default is false, no re-weighting correction)
{
  // Init StGlauberTree (read mode)
  mGlauberTree = new GlauberTree(0);

  // System should be like "AuAu_200GeV" (case insensitive)
  mCentralityMaker = new CentralityMaker(system) ;

  // Make sure table directory exists (0=directory exists)
  if ( gSystem->AccessPathName(tableDir.Data()) == 1 ){
    Error("GlauberAnalysisMaker", "can't find directory %s", tableDir.Data());
    assert(0);
  } 

  mNevents = 0 ;

  Init(tableDir) ;
}

//____________________________________________________________________________________________________
// Default destructor
GlauberAnalysisMaker::~GlauberAnalysisMaker()
{
  if(mCentralityMaker) delete mCentralityMaker ;
  if(mGlauberTree)     delete mGlauberTree ;
}

//____________________________________________________________________________________________________
Bool_t GlauberAnalysisMaker::Init(const TString tableDir)
{
  /// Skip if output ROOT file has already opened
  if(mOutputFile) return kTRUE ;

  mOutputFile = TFile::Open(mOutputFileName, "recreate");
  if(!mOutputFile || !mOutputFile->IsOpen() || mOutputFile->IsZombie()){
    Error("GlauberAnalysisMaker::Init", "can't open %s", mOutputFileName.Data());
    assert(0);
  }

  const TString title(mType) ;
  mImpactParameter = new GlauberHistogramMaker("ImpactParameter", title, "impact parameter b (fm)",
      GlauberConstUtilities::GetImpactParameterBin(), 0.0, GlauberConstUtilities::GetImpactParameterMax())
    ;

  mNpart = new GlauberHistogramMaker("Npart", title, "N_{part}",
        GlauberConstUtilities::GetNpartBin(), 0.0, GlauberConstUtilities::GetNpartMax())
    ;

  mNcoll = new GlauberHistogramMaker("Ncoll", title, "N_{coll}",
        GlauberConstUtilities::GetNcollBin(), 0.0, GlauberConstUtilities::GetNcollMax())
    ;
  
  mMultiplicity = new GlauberHistogramMaker("Multiplicity", title, "Multiplicity",
      GlauberConstUtilities::GetMultiplicityBin(), 0.0, GlauberConstUtilities::GetMultiplicityMax())
    ;

  // Aera
  const Int_t areaBin    = 100 ;
  const Double_t areaMin = 0.0 ;
  const Double_t areaMax = 50.0 ;
  mAreaRP = new GlauberHistogramMaker("AreaRP", title, "#LTS_{RP}#GT", areaBin, areaMin, areaMax);
  mAreaPP = new GlauberHistogramMaker("AreaPP", title, "#LTS_{PP}#GT", areaBin, areaMin, areaMax) ;

  // Eccentricity
  const Int_t eccBin    = 100 ;
  const Double_t eccMin = -1.0 ;
  const Double_t eccMax =  1.0 ;
  mEccRP  = new GlauberCumulantHistogramMaker("EccRP",  title, "#LT#varepsilon_{RP}#GT", eccBin, eccMin, eccMax);
  mEccRPM = new GlauberCumulantHistogramMaker("EccRPM",  title, "#LT#varepsilon_{RP}#GT", eccBin, eccMin, eccMax);

  for(Int_t io=0; io<3; io++){
    mEccPP[io]  = new GlauberCumulantHistogramMaker(Form("EccPP_%d", io),  title, Form("#LT#varepsilon_{PP,%d}#GT", io+2), eccBin/2, 0.0, eccMax) ;
    mEccPPM[io] = new GlauberCumulantHistogramMaker(Form("EccPPM_%d", io),  title, Form("#LT#varepsilon_{PP,%d}#GT", io+2), eccBin/2, 0.0, eccMax) ;
  }
  
  // Set table output directory
  mImpactParameter->SetTableDirectory(tableDir);
  mNpart->SetTableDirectory(tableDir);
  mNcoll->SetTableDirectory(tableDir);
  mMultiplicity->SetTableDirectory(tableDir);
  mAreaRP->SetTableDirectory(tableDir);
  mAreaPP->SetTableDirectory(tableDir);
  mEccRP->SetTableDirectory(tableDir);
  mEccRPM->SetTableDirectory(tableDir);
  for(Int_t io=0; io<3; io++){
    mEccPP[io]->SetTableDirectory(tableDir);
    mEccPPM[io]->SetTableDirectory(tableDir);
  }

  mOutputFile->GetList()->Sort() ;

  return kTRUE ;
}

//____________________________________________________________________________________________________
Bool_t GlauberAnalysisMaker::Make()
{
  /// Set x-axis

  const UInt_t centId         = 0 ; // Fix default (no need to change id unless one calculate multiplicity here)
//  const UInt_t centId         = mCentralityIdMap[mType] ; // 0:default, 1:small npp (large x), 2:large npp (small x)

  // Calculate multiplicity here
  // in order to deal with the change of centrality bins
  const Double_t multiplicity = mGlauberTree->GetMultiplicity() ;
//  const Double_t npart = static_cast<Double_t>(mGlauberTree->GetNpart()) ;
//  const Double_t ncoll = static_cast<Double_t>(mGlauberTree->GetNcoll()) ;
//  const Double_t multiplicity = mCentralityMaker->GetNegativeBinomial()->GetMultiplicity(npart, ncoll);

  // Re-weighting correction
  const Double_t reweighting = (mReweighting) ? mCentralityMaker->GetCentrality(centId)->GetReweighting(multiplicity) : 1.0 ;

  /// Discard events if mReweighting is true && the following condition is satisfied
  if( mReweighting && GlauberUtilities::instance()->GetUniform() > reweighting) return kFALSE ;

//  mImpactParameter->SetXaxis(*mGlauberTree, *(mCentralityMaker->GetCentrality(centId)), mType);
//  mNpart          ->SetXaxis(*mGlauberTree, *(mCentralityMaker->GetCentrality(centId)), mType);
//  mNcoll          ->SetXaxis(*mGlauberTree, *(mCentralityMaker->GetCentrality(centId)), mType);
//  mMultiplicity   ->SetXaxis(*mGlauberTree, *(mCentralityMaker->GetCentrality(centId)), mType);
//  mAreaRP         ->SetXaxis(*mGlauberTree, *(mCentralityMaker->GetCentrality(centId)), mType);
//  mAreaPP         ->SetXaxis(*mGlauberTree, *(mCentralityMaker->GetCentrality(centId)), mType);
//  mEccRP          ->SetXaxis(*mGlauberTree, *(mCentralityMaker->GetCentrality(centId)), mType);
//  mEccRPM         ->SetXaxis(*mGlauberTree, *(mCentralityMaker->GetCentrality(centId)), mType);
  mImpactParameter->SetXaxis(*mGlauberTree, *mCentralityMaker, mType);
  mNpart          ->SetXaxis(*mGlauberTree, *mCentralityMaker, mType);
  mNcoll          ->SetXaxis(*mGlauberTree, *mCentralityMaker, mType);
  mMultiplicity   ->SetXaxis(*mGlauberTree, *mCentralityMaker, mType);
  mAreaRP         ->SetXaxis(*mGlauberTree, *mCentralityMaker, mType);
  mAreaPP         ->SetXaxis(*mGlauberTree, *mCentralityMaker, mType);
  mEccRP          ->SetXaxis(*mGlauberTree, *mCentralityMaker, mType);
  mEccRPM         ->SetXaxis(*mGlauberTree, *mCentralityMaker, mType);

  for(Int_t io=0; io<3; io++){
//    mEccPP[io]->SetXaxis(*mGlauberTree, *(mCentralityMaker->GetCentrality(centId)), mType);
//    mEccPPM[io]->SetXaxis(*mGlauberTree, *(mCentralityMaker->GetCentrality(centId)), mType);
    mEccPP[io]->SetXaxis(*mGlauberTree,  *mCentralityMaker, mType);
    mEccPPM[io]->SetXaxis(*mGlauberTree, *mCentralityMaker, mType);
  }
  

  /// Fill impact parameter, Npart, Ncoll, ...
  const Double_t weight = (mUnitWeight) ? 1.0  // Unit weight (event-wise average)
    : static_cast<Double_t>( multiplicity ) ;  // Multiplicity weight (particle-wise average)

  // Unit weight
  mImpactParameter->Fill(mGlauberTree->GetB(), 1.0) ;
  mNpart->Fill(mGlauberTree->GetNpart(), 1.0) ;
  mNcoll->Fill(mGlauberTree->GetNcoll(), 1.0) ;
  mMultiplicity->Fill(multiplicity, 1.0) ;
  const Double_t areaRP = mGlauberTree->GetSRP(0) ;
  mAreaRP->Fill(areaRP, weight) ;

  const Double_t areaPP = mGlauberTree->GetSPP(0) ;
  if( areaPP > -9999. ) mAreaPP->Fill(areaPP, weight) ;

 
  // Reaction plane
  const Double_t eccRP = mGlauberTree->GetEccRP2(0) ;
  if( eccRP > -9999.) mEccRP->Fill(eccRP, weight) ;

  const Double_t eccRPM = mGlauberTree->GetEccRP2(2) ;
  if( eccRP > -9999.) mEccRPM->Fill(eccRPM, weight) ;

  // Participant plane
  for(Int_t io=0; io<3; io++){
    Double_t eccPP  = -9999. ;
    Double_t eccPPM = -9999. ;
    if( io == 0 ){      eccPP = mGlauberTree->GetEccPP2(0) ; eccPPM = mGlauberTree->GetEccPP2(2) ; }
    else if( io == 1 ){ eccPP = mGlauberTree->GetEccPP3(0) ; eccPPM = mGlauberTree->GetEccPP3(2) ; }
    else if( io == 2 ){ eccPP = mGlauberTree->GetEccPP4(0) ; eccPPM = mGlauberTree->GetEccPP4(2) ; }

    if( eccPP > -9999.)  mEccPP[io]->Fill(eccPP, weight) ;
    if( eccPPM > -9999.) mEccPPM[io]->Fill(eccPPM, weight) ;
  }

  return kTRUE ;
}

//____________________________________________________________________________________________________
Bool_t GlauberAnalysisMaker::RunFile(const TString inputFileName)
{
  mGlauberTree->Open(inputFileName);

  const Int_t nevents = mGlauberTree->GetEntries() ;

  for(Int_t ievent=0; ievent<nevents; ievent++){
    mGlauberTree->Clear() ;

    mGlauberTree->GetEntry(ievent);

    /// Make one event
    const Bool_t isEventOk = Make() ;
    if(isEventOk) mNevents++;
    
  }

  mGlauberTree->Close() ;

  return kTRUE ;
}

//____________________________________________________________________________________________________
Bool_t GlauberAnalysisMaker::Run(const TString inputFileList)
{
  ifstream fin(inputFileList.Data());
  if(!fin){
    Error("GlauberAnalysisMaker::run", "can't find %s", inputFileList.Data());
    assert(fin);
  }

  TString file ;
  while ( fin >> file ){
    RunFile(file);
  }

  return kTRUE ;
}

//____________________________________________________________________________________________________
Bool_t GlauberAnalysisMaker::Finish()
{
  /// Finish analysis
  /// 1. Correct particle-wise weight
  /// 2. Write text table
  /// 3. Write graph into outputs

  mOutputFile->cd();
  mImpactParameter->Finish(mType) ;
  mNpart->Finish(mType) ;
  mNcoll->Finish(mType) ;
  mMultiplicity->Finish(mType) ;
  mAreaRP->Finish(mType) ;
  mAreaPP->Finish(mType) ;
  mEccRP->Finish(mType) ;
  mEccRPM->Finish(mType) ;
  for(Int_t io=0; io<3; io++){
    mEccPP[io]->Finish(mType) ;
    mEccPPM[io]->Finish(mType) ;
  }

  mOutputFile->GetList()->Sort() ;

  /// Write/Close output ROOT file
  mOutputFile->Write();
  mOutputFile->Close();

  return kTRUE ;
}

//____________________________________________________________________________________________________
void GlauberAnalysisMaker::UnitWeightOn()
{
  mUnitWeight = kTRUE ;
}

//____________________________________________________________________________________________________
void GlauberAnalysisMaker::ReweightingOn()
{
  mReweighting = kTRUE ;
}

