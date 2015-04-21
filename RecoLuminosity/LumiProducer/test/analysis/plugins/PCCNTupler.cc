// ----------------------------------------------------------------------
// PCCNTupler
// ---------

#include <memory>
#include <string>
#include <iostream>
#include <fstream>
#include <vector>
#include <bitset>

#include "PCCNTupler.h"

#include "CondFormats/Alignment/interface/Definitions.h"
#include "CondFormats/RunInfo/interface/RunInfo.h"

#include "DataFormats/SiPixelDetId/interface/PixelBarrelName.h"
#include "DataFormats/SiPixelDetId/interface/PixelEndcapName.h"

#include "FWCore/Framework/interface/EventSetup.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/Framework/interface/ESHandle.h"
#include "FWCore/ServiceRegistry/interface/Service.h"

#include "Geometry/TrackerGeometryBuilder/interface/TrackerGeometry.h"

#include "DataFormats/SiPixelCluster/interface/SiPixelCluster.h"
#include "CommonTools/UtilAlgos/interface/TFileService.h"
DEFINE_FWK_MODULE(PCCNTupler);


#include <TROOT.h>
#include <TSystem.h>
#include <TTree.h>
#include <TFile.h>
#include <TH1F.h>
#include <TH2F.h>

using namespace std;
using namespace edm;
using namespace reco;

// ----------------------------------------------------------------------
PCCNTupler::PCCNTupler(edm::ParameterSet const& iConfig): 
    fVerbose(iConfig.getUntrackedParameter<int>("verbose", 0)),
    fPrimaryVertexCollectionLabel(iConfig.getUntrackedParameter<InputTag>("vertexCollLabel", edm::InputTag("offlinePrimaryVertices"))), 
    fPixelClusterLabel(iConfig.getUntrackedParameter<InputTag>("pixelClusterLabel", edm::InputTag("siPixelClusters"))), 
    saveType(iConfig.getUntrackedParameter<string>("saveType")),
    sampleType(iConfig.getUntrackedParameter<string>("sampleType"))
{
    cout << "----------------------------------------------------------------------" << endl;
    cout << "--- PCCNTupler constructor" << endl;

    nPrint = 0;
    edm::Service<TFileService> fs;

    includeVertexInformation = true;
    includePixels = true;

    tree = fs->make<TTree>("tree","Pixel Cluster Counters");
    tree->Branch("runNo",&runNo,"runNo/I");
    tree->Branch("LSNo",&LSNo,"LSNo/I");
    tree->Branch("LNNo",&LNNo,"LNNo/I");

    pileup = fs->make<TH1F>("pileup","pileup",100,0,100);
    if(includeVertexInformation){
        tree->Branch("nGoodVtx","map<int,int>",&nGoodVtx); 
        tree->Branch("num_Trks",&nTrk,"nTrk/I");
        recoVtxToken=consumes<reco::VertexCollection>(fPrimaryVertexCollectionLabel);
    }

    if(includePixels){
        tree->Branch("BXNo","map<int,int>",&BXNo);
        tree->Branch("nPixelClusters","map<int,int>",&nPixelClusters);
        tree->Branch("nClusters","map<int,int>",&nClusters);
        tree->Branch("layers","map<int,int>",&layers);
        // dead modules
        nDeadModules = 6;
        nDeadPrint = 0;
        deadModules[0] = 302125076; 
        deadModules[1] = 302125060;
        deadModules[2] = 302197516;
        deadModules[3] = 344019460;
        deadModules[4] = 344019464;
        deadModules[5] = 344019468;  
        pixelToken=consumes<edmNew::DetSetVector<SiPixelCluster> >(fPixelClusterLabel);
    }

}

// ----------------------------------------------------------------------
PCCNTupler::~PCCNTupler() { }  

// ----------------------------------------------------------------------
void PCCNTupler::endJob() { 
    cout << "==>PCCNTupler> Succesfully gracefully ended job" << endl;
}

// ----------------------------------------------------------------------
void PCCNTupler::beginJob() {

}


void PCCNTupler::beginLuminosityBlock(edm::LuminosityBlock const& lumi, edm::EventSetup const& isetup){
    firstEvent = true;
}

void PCCNTupler::endLuminosityBlock(edm::LuminosityBlock const& lumi, edm::EventSetup const& isetup){
    tree->Fill();
}


// ----------------------------------------------------------------------
void PCCNTupler::analyze(const edm::Event& iEvent, 
            const edm::EventSetup& iSetup)  {

    using namespace edm;
    using reco::VertexCollection;

    saveAndReset=false;
    sameEvent = (eventNo==(int)iEvent.id().event());
    sameLumiNib = true; // FIXME where is this info?
    sameLumiSect = (LSNo==(int)iEvent.getLuminosityBlock().luminosityBlock());
    
    // When arriving at the new LS, LN or event the tree 
    // must be filled and branches must be reset.
    // The final entry is saved in the deconstructor.
    saveAndReset = (saveType=="LumiSect" && !sameLumiSect)
                || (saveType=="LumiNib" && !sameLumiNib)
                || (saveType=="Event" && !sameEvent);
   
    if(   !saveAndReset && !sameLumiSect
       && !sameLumiNib  && !sameEvent) {
        std::cout<<"Diff LS, LN and Event, but not saving/resetting..."<<std::endl;
    }

    if(saveAndReset){
        if(!firstEvent) tree->Fill();
        nVtx = 0;
        nTrk = 0;
        nPixelClusters.clear();
        nClusters.clear();
        layers.clear();
        BXNo.clear();
        nGoodVtx.clear();
    }

    if(sampleType=="MC"){
        edm::Handle<std::vector< PileupSummaryInfo> > PupInfo;
        iEvent.getByLabel("addPileupInfo", PupInfo);
        std::vector<PileupSummaryInfo>::const_iterator PVI;
        for(PVI = PupInfo->begin(); PVI != PupInfo->end(); ++PVI) {
            int pu_bunchcrossing = PVI->getBunchCrossing();
            //std::cout<<"pu_bunchcrossing getPU_NumInteractions getTrueNumInteractions "<<pu_bunchcrossing<<" "<<PVI->getPU_NumInteractions()<<" "<<PVI->getTrueNumInteractions()<<std::endl;
            if(pu_bunchcrossing == 0) {
                pileup->Fill(PVI->getPU_NumInteractions());
            }
        }
    }

    // Get the Run, Lumi Section, and Event numbers, etc.
    runNo = iEvent.id().run();
    LSNo = iEvent.getLuminosityBlock().luminosityBlock();
    LNNo = -99; // FIXME need the luminibble
    eventNo = iEvent.id().event();
    timeStamp = iEvent.time().unixTime();
   
    if((BXNo.count(iEvent.bunchCrossing())==0||nGoodVtx.count(iEvent.bunchCrossing())==0) && !(BXNo.count(iEvent.bunchCrossing())==0&&nGoodVtx.count(iEvent.bunchCrossing())==0)){
        std::cout<<"BXNo and nGoodVtx should have the same keys but DO NOT!!!"<<std::endl;
    }
    
    if(BXNo.count(iEvent.bunchCrossing())==0){
        BXNo[iEvent.bunchCrossing()]=0;
    }

    if(nGoodVtx.count(iEvent.bunchCrossing())==0){
        nGoodVtx[iEvent.bunchCrossing()]=0;
    }

    BXNo[iEvent.bunchCrossing()]=BXNo[iEvent.bunchCrossing()]+1;
    // add the vertex information

    xV = -9999.; yV = -9999.; zV = -9999.;  chi2 = -9999.;  ndof = -9999;
   
    if(includeVertexInformation){
        edm::Handle<reco::VertexCollection> recVtxs;
        iEvent.getByToken(recoVtxToken,recVtxs);
        
   
        if(recVtxs.isValid()){
            for(reco::VertexCollection::const_iterator v=recVtxs->begin(); v!=recVtxs->end(); ++v){
                int nnTrk = v->tracksSize();
                int nd = (int)v->ndof();
                if(nd > 4 && v->isValid() && (v->isFake() == 0)){
                    nVtx++;
                    if(nnTrk > 0){
                        nGoodVtx[iEvent.bunchCrossing()]=nGoodVtx[iEvent.bunchCrossing()]+1;
                        if(nnTrk > nTrk){
                            nTrk = nnTrk;                   
                            //FIXME why are we multiplying by 10000 or 10?
                            xV = 10000.*v->x(); 
                            yV = 10000.*v->y(); 
                            zV = 10.*v->z();
                            chi2 = v->chi2();
                            ndof = (int)v->ndof();
                        }
                    }
                }
            }
        }
    }


    // -- Does this belong into beginJob()?
    ESHandle<TrackerGeometry> TG;
    iSetup.get<TrackerDigiGeometryRecord>().get(TG);

    // -- FED
    edm::eventsetup::EventSetupRecordKey recordKey(edm::eventsetup::EventSetupRecordKey::TypeTag::findType("RunInfoRcd"));
    if (recordKey.type() == edm::eventsetup::EventSetupRecordKey::TypeTag()) {
        //record not found
        cout << "Record \"RunInfoRcd" << "\" does not exist " << endl;
        fFED1 = 0; 
        fFED2 = (0x1 << 12) ; 
    } else {
        edm::ESHandle<RunInfo> runInfoHandle;
        iSetup.get<RunInfoRcd>().get(runInfoHandle);
    }
    // -- Pixel cluster
    edm::Handle< edmNew::DetSetVector<SiPixelCluster> > hClusterColl;
    iEvent.getByToken(pixelToken,hClusterColl);
    
    const edmNew::DetSetVector<SiPixelCluster> clustColl = *(hClusterColl.product());
    
    
    // ----------------------------------------------------------------------
    // -- Clusters without tracks

    for (TrackerGeometry::DetContainer::const_iterator it = TG->dets().begin(); it != TG->dets().end(); it++){
        //if (dynamic_cast<PixelGeomDetUnit*>((*it)) != 0){ 
            DetId detId = (*it)->geographicalId();


            // -- clusters on this det
            edmNew::DetSetVector<SiPixelCluster>::const_iterator isearch = clustColl.find(detId);
            if (isearch != clustColl.end()) {  // Not an empty iterator
                edmNew::DetSet<SiPixelCluster>::const_iterator  di;
                for (di = isearch->begin(); di != isearch->end(); ++di) {
                    if(nPixelClusters.count(detId())==0){
                        nPixelClusters[detId()]=0;
                    }
                    nPixelClusters[detId()] = nPixelClusters[detId()]+1;
                }

                int nCluster = isearch->size();
                if(nClusters.count(detId())==0){
                    nClusters[detId()]=0;
                }
                nClusters[detId()] += nCluster;

                if (detId.subdetId() == PixelSubdetector::PixelBarrel) {
                    PixelBarrelName detName = PixelBarrelName(detId);
                    int layer = detName.layerName();
                    if(layers.count(detId())==0){
                        layers[detId()]=layer;
                    }
                } else {
                    assert(detId.subdetId() == PixelSubdetector::PixelEndcap);
                    PixelEndcapName detName = PixelEndcapName(detId);
                    int disk = detName.diskName();
                    if(layers.count(detId())==0){
                        layers[detId()]=disk+3;
                    }
                }
            }
        //}
    }
} 

// define this as a plug-in


