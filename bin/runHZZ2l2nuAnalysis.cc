#include <iostream>
#include <boost/shared_ptr.hpp>

#include "CondFormats/JetMETObjects/interface/JetResolution.h"
#include "CondFormats/JetMETObjects/interface/JetCorrectionUncertainty.h"
#include "CondFormats/JetMETObjects/interface/JetCorrectorParameters.h"

#include "UserCode/llvv_fwk/interface/MacroUtils.h"
#include "UserCode/llvv_fwk/interface/SmartSelectionMonitor.h"
#include "UserCode/llvv_fwk/interface/DataEventSummaryHandler.h"
#include "UserCode/llvv_fwk/interface/TMVAUtils.h"
#include "UserCode/llvv_fwk/interface/LeptonEfficiencySF.h"
#include "UserCode/llvv_fwk/interface/PDFInfo.h"
#include "UserCode/llvv_fwk/interface/MuScleFitCorrector.h"
#include "UserCode/llvv_fwk/interface/GammaWeightsHandler.h"
#include "UserCode/llvv_fwk/interface/HiggsUtils.h"

#include "FWCore/FWLite/interface/AutoLibraryLoader.h"
#include "FWCore/PythonParameterSet/interface/MakeParameterSets.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "PhysicsTools/Utilities/interface/LumiReWeighting.h"

#include "TSystem.h"
#include "TFile.h"
#include "TTree.h"
#include "TCanvas.h"
#include "TH1F.h"
#include "TH2F.h"
#include "TProfile.h"
#include "TEventList.h"
#include "TROOT.h"
 
using namespace std;

TString getJetRegion(float eta)
{
  TString reg("TK");
  if(fabs(eta)>2.5)  reg="HEin";
  if(fabs(eta)>2.75) reg="HEout";
  if(fabs(eta)>3)    reg="HF";
  return reg;
}

int main(int argc, char* argv[])
{
  //##############################################
  //########    GLOBAL INITIALIZATION     ########
  //##############################################

  // check arguments
  if(argc<2){ std::cout << "Usage : " << argv[0] << " parameters_cfg.py" << std::endl; exit(0); }
  
  // load framework libraries
  gSystem->Load( "libFWCoreFWLite" );
  AutoLibraryLoader::enable();
  
  // configure the process
  const edm::ParameterSet &runProcess = edm::readPSetsFrom(argv[1])->getParameter<edm::ParameterSet>("runProcess");

  bool isMC       = runProcess.getParameter<bool>("isMC");
  int mctruthmode = runProcess.getParameter<int>("mctruthmode");

  TString suffix=runProcess.getParameter<std::string>("suffix");
  std::vector<std::string> urls=runProcess.getParameter<std::vector<std::string> >("input");
  TString url=TString(urls[0]);
  TString outFileUrl(gSystem->BaseName(url)); 
  outFileUrl.ReplaceAll(".root",""); 
  outFileUrl+=suffix;
  if(mctruthmode!=0) { outFileUrl += "_filt"; outFileUrl += mctruthmode; }
  TString outdir=runProcess.getParameter<std::string>("outdir");
  TString outUrl( outdir );
  gSystem->Exec("mkdir -p " + outUrl);
  bool filterOnlyEE(false), filterOnlyMUMU(false), filterOnlyEMU(false);
  if(!isMC)
    {
      if(url.Contains("DoubleEle")) filterOnlyEE=true;
      if(url.Contains("DoubleMu"))  filterOnlyMUMU=true;
      if(url.Contains("MuEG"))      filterOnlyEMU=true;

    }
  bool isSingleMuPD(!isMC && url.Contains("SingleMu"));  
  bool isV0JetsMC(isMC && (url.Contains("DYJetsToLL_50toInf") || url.Contains("WJets")));
  bool isMC_GG  = isMC && ( string(url.Data()).find("GG" )  != string::npos);
  bool isMC_VBF = isMC && ( string(url.Data()).find("VBF")  != string::npos);
  bool isMC_ZZ  = isMC && ( string(url.Data()).find("MC8TeV_ZZ")  != string::npos);
  bool isMC_WZ  = isMC && ( string(url.Data()).find("MC8TeV_WZ")  != string::npos);

  TString outTxtUrl= outUrl + "/" + outFileUrl + ".txt";
  FILE* outTxtFile = NULL;
  //if(!isMC)
  outTxtFile = fopen(outTxtUrl.Data(), "w");
  printf("TextFile URL = %s\n",outTxtUrl.Data());

  //tree info
  TString dirname = runProcess.getParameter<std::string>("dirName");

  //systematics
  bool runSystematics                        = runProcess.getParameter<bool>("runSystematics");
  std::vector<TString> varNames(1,"");
  if(runSystematics){
    varNames.push_back("_jerup");    varNames.push_back("_jerdown");
    varNames.push_back("_jesup");    varNames.push_back("_jesdown");  
    varNames.push_back("_umetup");   varNames.push_back("_umetdown");  
    varNames.push_back("_lesup");    varNames.push_back("_lesdown");  
    varNames.push_back("_puup");     varNames.push_back("_pudown");  
    varNames.push_back("_btagup");   varNames.push_back("_btagdown");
    if(isMC_ZZ)             { varNames.push_back("_zzptup");   varNames.push_back("_zzptdown");     }
    if(isMC_WZ)             { varNames.push_back("_wzptup");   varNames.push_back("_wzptdown");     }
    if(isMC_GG || isMC_VBF) { varNames.push_back("_lshapeup"); varNames.push_back("_lshapedown"); }
  }
  size_t nvarsToInclude=varNames.size();
  
  std::string weightsDir = runProcess.getParameter<std::vector<std::string> >("weightsFile")[0];

  //shape uncertainties for dibosons
  std::vector<TGraph *> vvShapeUnc;
  if(isMC_ZZ || isMC_WZ)
    {
      TString weightsFile=weightsDir+"/zzQ2unc.root";
      TString dist("zzpt");
      if(isMC_WZ) { weightsFile.ReplaceAll("zzQ2","wzQ2"); dist.ReplaceAll("zzpt","wzpt"); }
      gSystem->ExpandPathName(weightsFile);
      TFile *q2UncF=TFile::Open(weightsFile);
      vvShapeUnc.push_back( new TGraph( (TH1 *)q2UncF->Get(dist+"_up") ) );
      vvShapeUnc.push_back( new TGraph( (TH1 *)q2UncF->Get(dist+"_down") ) );
      q2UncF->Close();
    }

  //HIGGS weights and uncertainties
  
  //narrow resonance
  double cprime = runProcess.getParameter<double>("cprime");
  double brnew  = runProcess.getParameter<double>("brnew");
  std::vector<std::pair<double, double> > NRparams;
  NRparams.push_back(std::make_pair<double,double>(double(cprime),double(brnew)) );
  if(suffix==""){ //consider the other points only when no suffix is being used
    NRparams.push_back(std::make_pair<double,double>(0.1, 0) );
    NRparams.push_back(std::make_pair<double,double>(0.3, 0) );
    NRparams.push_back(std::make_pair<double,double>(0.2, 0) );
    NRparams.push_back(std::make_pair<double,double>(0.4, 0) );
    NRparams.push_back(std::make_pair<double,double>(0.5, 0) );
    NRparams.push_back(std::make_pair<double,double>(0.6, 0) );
    NRparams.push_back(std::make_pair<double,double>(0.8, 0) );
    NRparams.push_back(std::make_pair<double,double>(1.0, 0) );
  }
  std::vector<TGraph *> NRweightsGr;
  std::vector<double> NRweights(NRparams.size());
  std::vector<TString>NRsuffix; for(unsigned int nri=0;nri<NRparams.size();nri++){if(NRparams[nri].first<0 && NRparams[nri].second<0){NRsuffix.push_back(TString(""));}else{char tmp[255];sprintf(tmp,"_cp%3.2f_brn%3.2f",NRparams[nri].first, NRparams[nri].second); NRsuffix.push_back(TString(tmp));} }

  
  //STANDARD MODEL
  double HiggsMass=0; string VBFString = ""; string GGString("");
  TF1 *decayProbPdf=new TF1("relbw","(2*sqrt(2)*[0]*[1]*sqrt(pow([0],2)*(pow([0],2)+pow([1],2)))/(TMath::Pi()*sqrt(pow([0],2)+sqrt(pow([0],2)*(pow([0],2)+pow([1],2))))))/(pow(pow(x,2)-pow([0],2),2)+pow([0]*[1],2))",0,2000);
  if(isMC_GG){  
    size_t GGStringpos =  string(url.Data()).find("GG");
    string StringMass = string(url.Data()).substr(GGStringpos+5,4);  sscanf(StringMass.c_str(),"%lf",&HiggsMass);
    GGString = string(url.Data()).substr(GGStringpos);  
  }else if(isMC_VBF){
    size_t VBFStringpos =  string(url.Data()).find("VBF");
    string StringMass = string(url.Data()).substr(VBFStringpos+6,4);  sscanf(StringMass.c_str(),"%lf",&HiggsMass);
    VBFString = string(url.Data()).substr(VBFStringpos);
  }
  
  //#######################################
  //####      LINE SHAPE WEIGHTS       ####
  //#######################################
  TString lineShapeWeightsFileURL(weightsDir+"/");
  lineShapeWeightsFileURL += (isMC_VBF ? "VBFtoHtoZZLineShapes.root" : "GGtoHtoZZLineShapes.root");
  gSystem->ExpandPathName(lineShapeWeightsFileURL);
  TFile *fin=TFile::Open(lineShapeWeightsFileURL);     

  TString interferenceShapeWeightsFileUrl(weightsDir+"/");
  interferenceShapeWeightsFileUrl += (isMC_VBF ? "VBFtoHtoZZLineShapesInterference.root" : "GGtoHtoZZLineShapesInterference.root");
  TFile *fin_int=0;
  if(interferenceShapeWeightsFileUrl!="" && isMC_GG) 
    {
      gSystem->ExpandPathName(interferenceShapeWeightsFileUrl);
      fin_int=TFile::Open(interferenceShapeWeightsFileUrl);
    }

  TH1 *hGen=0;
  TGraph *hLineShapeNominal=0;
  std::map<std::pair<double,double>, std::vector<TGraph *> > hLineShapeGrVec;  
  if(fin && (isMC_GG || isMC_VBF))
    {
      cout << "Line shape weights (and uncertainties) will be applied from " << fin->GetName() << endl;
      if(fin_int)
	cout << "Inteference terms (and uncertaintnies) will be replaced from " << fin_int->GetName() << endl;

      char dirBuf[100];
      sprintf(dirBuf,"H%d/",int(HiggsMass));
      
      hLineShapeNominal      = new TGraph((TH1 *)fin->Get(dirBuf+TString("cps_shape")));
      hGen                   = (TH1 *) fin->Get(dirBuf+TString("gen")); hGen->SetDirectory(0); hGen->Scale(1./hGen->Integral());
      
      TGraph *cpsGr          = (TGraph *) fin->Get(dirBuf+TString("cps"));
      TGraph *cpspintGr      = (TGraph *) (fin_int!=0? fin_int: fin)->Get(dirBuf+TString("nominal"));
      TGraph *cpspint_upGr   = (TGraph *) (fin_int!=0? fin_int: fin)->Get(dirBuf+TString("up"));
      TGraph *cpspint_downGr = (TGraph *) (fin_int!=0? fin_int: fin)->Get(dirBuf+TString("down"));
      if(cpspintGr==0)
	{
	  cpspintGr = (TGraph *)cpsGr->Clone();
	  for(int ip=0; ip<cpspintGr->GetN(); ip++) { Double_t x,y; cpspintGr->GetPoint(ip,x,y); cpspintGr->SetPoint(ip,x,1); }
	  cpspint_upGr = (TGraph *) cpspintGr->Clone();
	  cpspint_downGr=(TGraph *) cpspintGr->Clone();
	}
      
      //loop over possible scenarios
      for(size_t nri=0; nri<NRparams.size(); nri++)
	{
	  //recompute weights depending on the scenario (SM or BSM)
	  TGraph *shapeWgtsGr      = new TGraph; shapeWgtsGr->SetName("shapeWgts_"+ NRsuffix[nri]);          float shapeNorm(0);
	  TGraph *shapeWgts_upGr   = new TGraph; shapeWgts_upGr->SetName("shapeWgtsUp_"+ NRsuffix[nri]);     float shapeUpNorm(0);
	  TGraph *shapeWgts_downGr = new TGraph; shapeWgts_downGr->SetName("shapeWgtsDown_"+ NRsuffix[nri]); float shapeDownNorm(0);
	  for(int ip=1; ip<=hGen->GetXaxis()->GetNbins(); ip++)
	    {
	      Double_t hmass    = hGen->GetBinCenter(ip);
	      Double_t hy       = hGen->GetBinContent(ip);

	      Double_t shapeWgt(1.0),shapeWgtUp(1.0),shapeWgtDown(1.0);
	      if(NRparams[nri].first<0)
		{
		  shapeWgt     = cpsGr->Eval(hmass) * cpspintGr->Eval(hmass);
		  shapeWgtUp   = cpsGr->Eval(hmass) * cpspint_upGr->Eval(hmass);
		  shapeWgtDown = cpsGr->Eval(hmass) * cpspint_downGr->Eval(hmass);
		}
	      else
		{
		  Double_t nrWgt = higgs::utils::weightNarrowResonnance(VBFString,HiggsMass, hmass, NRparams[nri].first, NRparams[nri].second, hLineShapeNominal,decayProbPdf);
		  shapeWgt       = cpsGr->Eval(hmass) * nrWgt;
		  shapeWgtUp     = shapeWgt;
		  shapeWgtDown   = shapeWgt;
		}
		            
	      shapeWgtsGr->SetPoint(shapeWgtsGr->GetN(),           hmass, shapeWgt);       shapeNorm     += shapeWgt*hy;
	      shapeWgts_upGr->SetPoint(shapeWgts_upGr->GetN(),     hmass, shapeWgtUp);     shapeUpNorm   += shapeWgtUp*hy;
	      shapeWgts_downGr->SetPoint(shapeWgts_downGr->GetN(), hmass, shapeWgtDown);   shapeDownNorm += shapeWgtDown*hy;
	    }

	  //fix possible normalization issues
	  cout << "C'=" << NRparams[nri].first << " BRnew=" << NRparams[nri].second << " shape wgts will be re-normalized with: "
	       << " nominal=" << shapeNorm
	       << " up     =" << shapeUpNorm
	       << " down   =" << shapeDownNorm 
	       << endl;
	  for(Int_t ip=0; ip<shapeWgtsGr->GetN(); ip++)
	    {
	      Double_t x,y;
	      shapeWgtsGr->GetPoint(ip,x,y);
	      shapeWgtsGr->SetPoint(ip,x,y/shapeNorm);

	      shapeWgts_upGr->GetPoint(ip,x,y);
	      shapeWgts_upGr->SetPoint(ip,x,y/shapeUpNorm);

	      shapeWgts_downGr->GetPoint(ip,x,y);
	      shapeWgts_downGr->SetPoint(ip,x,y/shapeDownNorm);

	    }

	  //all done here...
	  std::vector<TGraph *> inrWgts;
	  inrWgts.push_back( shapeWgtsGr      );
	  inrWgts.push_back( shapeWgts_upGr   );
	  inrWgts.push_back( shapeWgts_downGr );
	  hLineShapeGrVec[ NRparams[nri] ] = inrWgts;
	}

      //close files
      fin->Close();
      delete fin;
      if(fin_int){
	fin_int->Close();
	delete fin_int;
      }
    }


  //##############################################
  //########    INITIATING HISTOGRAMS     ########
  //##############################################
  SmartSelectionMonitor mon;

  //generator level control : add an underflow entry to make sure the histo is kept
  ((TH1F*)mon.addHistogram( new TH1F( "higgsMass_raw",     ";Higgs Mass [GeV];Events", 500,0,1500) ))->Fill(-1.0,0.0001);
  ((TH1F*)mon.addHistogram( new TH1F( "higgsMass_cpspint", ";Higgs Mass [GeV];Events", 500,0,1500) ))->Fill(-1.0,0.0001);
  for(unsigned int nri=0;nri<NRparams.size();nri++){ 
    ((TH1F*)mon.addHistogram( new TH1F( "higgsMass_4nr"+NRsuffix[nri] , ";Higgs Mass;Events [GeV]", 500,0,1500) ))->Fill(-1.0,0.0001);
  }

  //event selection
  TH1F* Hcutflow  = (TH1F*) mon.addHistogram(  new TH1F ("cutflow"    , "cutflow"    ,6,0,6) ) ;
  TH1F *h=(TH1F*) mon.addHistogram( new TH1F ("eventflow", ";;Events", 8,0,8) );
  h->GetXaxis()->SetBinLabel(1,"#geq 2 iso leptons");
  h->GetXaxis()->SetBinLabel(2,"|M-91|<15");
  h->GetXaxis()->SetBinLabel(3,"p_{T}>55");
  h->GetXaxis()->SetBinLabel(4,"3^{rd}-lepton veto");
  h->GetXaxis()->SetBinLabel(5,"b-veto"); 
  h->GetXaxis()->SetBinLabel(6,"#Delta #phi(jet,E_{T}^{miss})>0.5");
  h->GetXaxis()->SetBinLabel(7,"E_{T}^{miss}>70");

  //pu control
  mon.addHistogram( new TH1F( "nvtx",";Vertices;Events",50,0,50) ); 
  mon.addHistogram( new TH1F( "nvtxraw",";Vertices;Events",50,0,50) ); 
  mon.addHistogram( new TH1F( "rho",";#rho;Events",50,0,25) ); 

  //lepton control
  mon.addHistogram( new TH1F( "leadpt",     ";Transverse momentum [GeV];Events", 50,0,500) );
  mon.addHistogram( new TH1F( "leadeta",    ";Pseudo-rapidity;Events", 50,0,2.6) );
  mon.addHistogram( new TH1F( "trailerpt",  ";Transverse momentum [GeV];Events", 50,0,500) );
  mon.addHistogram( new TH1F( "trailereta", ";Pseudo-rapidity;Events", 50,0,2.6) );
  mon.addHistogram( new TH1F( "zy",         ";Rapidity;Events", 50,0,3) );
  mon.addHistogram( new TH1F( "zmass",      ";Mass [GeV];Events", 100,40,250) );
  mon.addHistogram( new TH1F( "qt",         ";Transverse momentum [GeV];Events / (1 GeV)",1500,0,1500));
  mon.addHistogram( new TH1F( "qtraw",      ";Transverse momentum [GeV];Events / (1 GeV)",1500,0,1500));

  //extra leptons in the event
  mon.addHistogram( new TH1F( "nextraleptons", ";Extra leptons;Events",4,0,4) );
  mon.addHistogram( new TH1F( "thirdleptonpt", ";Transverse momentum;Events", 50,0,500) );
  mon.addHistogram( new TH1F( "thirdleptoneta", ";Pseudo-rapidity;Events", 50,0,2.6) );
  mon.addHistogram( new TH1F( "thirdleptonmt", ";Transverse mass(3^{rd} lepton,E_{T}^{miss}) [GeV];Events", 50,0,500) );


  mon.addHistogram( new TH1F("jp",       ";Jet probability;Jets",50,0.,3.) );
  mon.addHistogram( new TH1F("jpb",      ";Jet probability;Jets",50,0.,3.) );
  mon.addHistogram( new TH1F("jpc",      ";Jet probability;Jets",50,0.,3.) );
  mon.addHistogram( new TH1F("jpothers", ";Jet probability;Jets",50,0.,3.) );
  TH1 *hbtags=mon.addHistogram( new TH1F("nbtags",   ";b-tag multiplicity;Events",5,0,5) );
  mon.addHistogram( new TH1F("leadjetpt",    ";Transverse momentum [GeV];Events",50,0,1000) );
  mon.addHistogram( new TH1F("trailerjetpt", ";Transverse momentum [GeV];Events",50,0,1000) );
  mon.addHistogram( new TH1F("fwdjeteta",    ";Pseudo-rapidity;Events",25,0,5) );
  mon.addHistogram( new TH1F("cenjeteta",       ";Pseudo-rapidity;Events",25,0,5) );
   Double_t mjjaxis[32];
  mjjaxis[0]=0.01;
  for(size_t i=1; i<20; i++)  mjjaxis[i]   =50*i;        //0-1000
  for(size_t i=0; i<5; i++)   mjjaxis[20+i]=1000+100*i; //1000-1500
  for(size_t i=0; i<=5; i++)   mjjaxis[25+i]=1500+300*i; //1500-5000  
  mjjaxis[31]=5000;
  mon.addHistogram( new TH1F("vbfmjj"       , ";Dijet invariant mass [GeV];Events",31,mjjaxis) );
  mon.addHistogram( new TH1F("vbfdphijj"    , ";Azimuthal angle difference;Events",20,0,3.5) );
  mon.addHistogram( new TH1F("vbfdetajj"    , ";Pseudo-rapidity span;Events",20,0,10) );
  TH1 *hjets=mon.addHistogram( new TH1F("njets",  ";Jet multiplicity;Events",5,0,5) );
  for(int ibin=1; ibin<=hjets->GetXaxis()->GetNbins(); ibin++)
    {
      TString label("");
      if(ibin==h->GetXaxis()->GetNbins()) label +="#geq";
      else                                label +="=";
      label += (ibin-1);
      hjets->GetXaxis()->SetBinLabel(ibin,label);
      hbtags->GetXaxis()->SetBinLabel(ibin,label);
    } 

  mon.addHistogram( new TH1F( "mindphijmet",  ";min #Delta#phi(jet,E_{T}^{miss});Events",40,0,4) );
  mon.addHistogram( new TH1D( "balance",      ";E_{T}^{miss}/q_{T};Events", 25,0,2.5) );
  mon.addHistogram( new TH1F( "met",          ";Missing transverse energy [GeV];Events", 50,0,500) );
  mon.addHistogram( new TH1F( "axialmet",     ";Axial missing transvere energy [GeV];Events", 50,-100,150) );
  mon.addHistogram( new TH1F( "mt"  ,         ";Transverse mass;Events", 100,0,1000) );
  mon.addHistogram( new TH1F( "mtresponse",   ";Transverse mass response;Events", 100,0,2) );

  //
  // STATISTICAL ANALYSIS
  //
  std::vector<double> optim_Cuts1_met; 
  for(double met=50;met<140;met+=5) {  optim_Cuts1_met    .push_back(met);  }
  TProfile* Hoptim_cuts1_met     =  (TProfile*) mon.addHistogram( new TProfile ("optim_cut1_met"    , ";cut index;met"    ,optim_Cuts1_met.size(),0,optim_Cuts1_met.size()) ) ;
  for(unsigned int index=0;index<optim_Cuts1_met.size();index++){ Hoptim_cuts1_met    ->Fill(index, optim_Cuts1_met[index]);  }
  TH1F* Hoptim_systs     =  (TH1F*) mon.addHistogram( new TH1F ("optim_systs"    , ";syst;", nvarsToInclude,0,nvarsToInclude) ) ;
  for(size_t ivar=0; ivar<nvarsToInclude; ivar++)
    {
      Hoptim_systs->GetXaxis()->SetBinLabel(ivar+1, varNames[ivar]);

      for(unsigned int nri=0;nri<NRparams.size();nri++){ 
	mon.addHistogram( new TH2F (TString("mt_shapes")+NRsuffix[nri]+varNames[ivar],";cut index;Transverse mass [GeV];Events",optim_Cuts1_met.size(),0,optim_Cuts1_met.size(), 160,150,1750) );     
	mon.addHistogram( new TH2F (TString("met_shapes")+NRsuffix[nri]+varNames[ivar],";cut index;Missing transverse energy [GeV];Events",optim_Cuts1_met.size(),0,optim_Cuts1_met.size(),100 ,0,500) );     
	TH2F *h=(TH2F *) mon.addHistogram( new TH2F ("mt_shapes_NRBctrl"+NRsuffix[nri]+varNames[ivar],";cut index;Selection region;Events",optim_Cuts1_met.size(),0,optim_Cuts1_met.size(),6,0,6) );
	h->GetYaxis()->SetBinLabel(1,"M_{in}^{ll}/=0 b-tags");
	h->GetYaxis()->SetBinLabel(2,"M_{out}^{ll}/=0 b-tags");
	h->GetYaxis()->SetBinLabel(3,"M_{out+}^{ll}/=0 b-tags");
	h->GetYaxis()->SetBinLabel(4,"M_{in}^{ll}/#geq 1 b-tag");
	h->GetYaxis()->SetBinLabel(5,"M_{out}^{ll}/#geq 1 b-tag");
	h->GetYaxis()->SetBinLabel(6,"M_{out+}^{ll}/#geq 1 b-tag");
     }
    }
     
  //##############################################
  //######## GET READY FOR THE EVENT LOOP ########
  //##############################################

  //open the file and get events tree
  DataEventSummaryHandler evSummaryHandler;
  TFile *file = TFile::Open(url);
  printf("Looping on %s\n",url.Data());
  if(file==0) return -1;
  if(file->IsZombie()) return -1;
  if( !evSummaryHandler.attach( (TTree *) file->Get(dirname+"/data") , false) ) { file->Close();  return -1; }

  //check run range to compute scale factor (if not all entries are used)
  const Int_t totalEntries= evSummaryHandler.getEntries();
  
  //MC normalization (to 1/pb)
  float cnorm=1.0;
  if(isMC){
    TH1F* cutflowH = (TH1F *) file->Get(dirname+"/cutflow");
    if(cutflowH) cnorm=cutflowH->GetBinContent(1);
    printf("cnorm = %f\n",cnorm);
  }
  Hcutflow->SetBinContent(1,cnorm);

  //jet energy scale and uncertainties 
  TString jecDir = runProcess.getParameter<std::string>("jecDir");
  gSystem->ExpandPathName(jecDir);
  FactorizedJetCorrector *jesCor        = utils::cmssw::getJetCorrector(jecDir,isMC);
  JetCorrectionUncertainty *totalJESUnc = new JetCorrectionUncertainty((jecDir+"/MC_Uncertainty_AK5PFchs.txt").Data());
  
  //muon energy scale and uncertainties
  MuScleFitCorrector *muCor=getMuonCorrector(jecDir,url);

  //lepton efficiencies
  LeptonEfficiencySF lepEff;

  //pileup weighting
  std::vector<double> dataPileupDistributionDouble = runProcess.getParameter< std::vector<double> >("datapileup");
  std::vector<float> dataPileupDistribution; for(unsigned int i=0;i<dataPileupDistributionDouble.size();i++){dataPileupDistribution.push_back(dataPileupDistributionDouble[i]);}
  std::vector<float> mcPileupDistribution;
  if(isMC){
    TString puDist(dirname+"/pileup");
    TH1F* histo = (TH1F *) file->Get(puDist);
    if(!histo) std::cout<<"pileup histogram is null!!!\n";
    for(int i=1;i<=histo->GetNbinsX();i++){mcPileupDistribution.push_back(histo->GetBinContent(i));}
    delete histo;
  }
  while(mcPileupDistribution.size()<dataPileupDistribution.size())  mcPileupDistribution.push_back(0.0);
  while(mcPileupDistribution.size()>dataPileupDistribution.size())dataPileupDistribution.push_back(0.0);
  
  gROOT->cd();  //THIS LINE IS NEEDED TO MAKE SURE THAT HISTOGRAM INTERNALLY PRODUCED IN LumiReWeighting ARE NOT DESTROYED WHEN CLOSING THE FILE
  edm::LumiReWeighting *LumiWeights= isMC ? new edm::LumiReWeighting(mcPileupDistribution,dataPileupDistribution): 0;
  utils::cmssw::PuShifter_t PuShifters;
  if(isMC) { PuShifters=utils::cmssw::getPUshifters(dataPileupDistribution,0.05); }


  higgs::utils::EventCategory eventCategoryInst(higgs::utils::EventCategory::EXCLUSIVE2JETSVBF); //jet(0,>=1)+vbf binning


  //##############################################
  //########           EVENT LOOP         ########
  //##############################################
  //loop on all the events
  printf("Progressing Bar     :0%%       20%%       40%%       60%%       80%%       100%%\n");
  printf("Scanning the ntuple :");
  int treeStep(totalEntries/50);
  DuplicatesChecker duplicatesChecker;
  int nDuplicates(0);
  for( int iev=0; iev<totalEntries; iev++){
      if(iev%treeStep==0){printf(".");fflush(stdout);}

      //##############################################   EVENT LOOP STARTS   ##############################################
      //load the event content from tree
      evSummaryHandler.getEntry(iev);
      DataEventSummary &ev=evSummaryHandler.getEvent();
      if(!isMC && duplicatesChecker.isDuplicate( ev.run, ev.lumi, ev.event) ) { nDuplicates++; continue; }

      if(isV0JetsMC){
	mon.fillHisto("nup","",ev.nup,1);
	if(ev.nup>5) continue;
	mon.fillHisto("nupfilt","",ev.nup,1);
      }

      //physics objects
      data::PhysicsObjectCollection_t photons = evSummaryHandler.getPhysicsObject(DataEventSummaryHandler::PHOTONS);
      data::PhysicsObjectCollection_t leptons = evSummaryHandler.getPhysicsObject(DataEventSummaryHandler::LEPTONS);
      data::PhysicsObjectCollection_t jets    = evSummaryHandler.getPhysicsObject(DataEventSummaryHandler::JETS);
      data::PhysicsObjectCollection_t recoMet = evSummaryHandler.getPhysicsObject(DataEventSummaryHandler::MET);
      data::PhysicsObjectCollection_t gen     = evSummaryHandler.getPhysicsObject(DataEventSummaryHandler::GENPARTICLES);      

      //require compatibilitiy of the event with the PD
      bool eeTrigger          = ev.t_bits[0];
      bool muTrigger          = ev.t_bits[6];
      bool mumuTrigger        = ev.t_bits[2] || ev.t_bits[3] || muTrigger;
      bool emuTrigger         = ev.t_bits[4] || ev.t_bits[5];
      if(filterOnlyEE)   { mumuTrigger=false; emuTrigger=false;  }
      if(filterOnlyMUMU) { eeTrigger=false;   emuTrigger=false;  }
      if(isSingleMuPD)   { eeTrigger=false;   emuTrigger=false;  if( mumuTrigger || !muTrigger ) mumuTrigger= false;  }
      if(filterOnlyEMU)  { eeTrigger=false;   mumuTrigger=false; }

      bool hasPhotonTrigger(false);
      float triggerPrescale(1.0),triggerThreshold(0);
      bool runPhotonSelection(mctruthmode==22 || mctruthmode==111);
      if(runPhotonSelection)
	{
	  eeTrigger=false; mumuTrigger=false;
	  for(size_t itrig=10; itrig>=7; itrig--)
	    {
	      if(!ev.t_bits[itrig]) continue;
	      hasPhotonTrigger=true;
	      triggerPrescale=ev.t_prescale[itrig];
	      if(itrig==10) triggerThreshold=90;
	      if(itrig==9)  triggerThreshold=75;
	      if(itrig==8)  triggerThreshold=50;
	      if(itrig==7)  triggerThreshold=36;
	      break;
	    }
	}

      //
      // DERIVE WEIGHTS TO APPLY TO SAMPLE
      //

      //pileup weight
      float weight = 1.0;
      double TotalWeight_plus = 1.0;
      double TotalWeight_minus = 1.0;
      float puWeight(1.0);
      if(isMC){
        puWeight          = LumiWeights->weight(ev.ngenITpu);
	weight            = puWeight;
        TotalWeight_plus  = PuShifters[utils::cmssw::PUUP]->Eval(ev.ngenITpu);
        TotalWeight_minus = PuShifters[utils::cmssw::PUDOWN]->Eval(ev.ngenITpu);
      }

      //Higgs specific weights
      float lShapeWeights[3]={1.0,1.0,1.0};
      for(unsigned int nri=0;nri<NRparams.size();nri++){NRweights[nri] = 1.0;}
      if(isMC){

	LorentzVector higgs(0,0,0,0);
	for(size_t igen=0; igen<gen.size(); igen++){
	  if(gen[igen].get("status")!=3) continue;
	  if(gen[igen].get("id")!=25) continue;
	  higgs=gen[igen];
	}
	
	float shapeWeight(1.0);
        if((isMC_VBF || isMC_GG) && higgs.pt()>0){
	  {
	    //Line shape weights 
	    if(isMC_VBF || isMC_GG)
	      {
		std::vector<TGraph *> nominalShapeWgtGr=hLineShapeGrVec.begin()->second;
		for(size_t iwgt=0; iwgt<nominalShapeWgtGr.size(); iwgt++)
		  {
		    if(nominalShapeWgtGr[iwgt]==0) continue;
		    lShapeWeights[iwgt]=nominalShapeWgtGr[iwgt]->Eval(higgs.mass());
		  }
	      }
	    shapeWeight   = lShapeWeights[0];
	    
	    //control SM line shape
	    mon.fillHisto("higgsMass_raw",    "", higgs.mass(), puWeight);
	    mon.fillHisto("higgsMass_cpspint","", higgs.mass(), puWeight * shapeWeight);
	    
	    //compute weight correction for narrow resonnance
	    for(unsigned int nri=0;nri<NRparams.size();nri++){ 
	      if(NRparams[nri].first<0) continue;
	      std::vector<TGraph *> shapeWgtGr = hLineShapeGrVec[NRparams[nri] ];
	      NRweights[nri] = shapeWgtGr[0]->Eval(higgs.mass()); 
	      float iweight = puWeight * NRweights[nri];
	      mon.fillHisto(TString("higgsMass_4nr")+NRsuffix[nri], "", higgs.mass(), iweight );
	    }  
	  }
	}
  
	//final event weight
	weight = puWeight * shapeWeight;
      }
      Hcutflow->Fill(1,1);
      Hcutflow->Fill(2,weight);
      Hcutflow->Fill(3,weight*TotalWeight_minus);
      Hcutflow->Fill(4,weight*TotalWeight_plus);
      Hcutflow->Fill(5,1.0);

      //
      //
      // BELOW FOLLOWS THE ANALYSIS OF THE MAIN SELECTION WITH N-1 PLOTS
      //
      //

      //
      // photon selection
      //
      data::PhysicsObjectCollection_t selPhotons;
      if(runPhotonSelection)
	{
	  //filter out number of prompt photons to avoid double counting
	  int ngenpho(0);
	  for(size_t igen=0; igen<gen.size(); igen++)
	    {
	      if(gen[igen].get("id")!=22 || gen[igen].get("status")!=1) continue;
	      float lxy=gen[igen].getVal("lxy");
	      if(lxy>0) continue;
	      ngenpho++;
	    }
	  if(mctruthmode==111 && ngenpho>0) continue;
	  //if(mctruthmode==22 && ngenpho==0) continue;

	  //select the photons
	  for(size_t ipho=0; ipho<photons.size(); ipho++)
	    {
	      double pt=photons[ipho].pt();
	      double eta=photons[ipho].getVal("sceta");

	      //if systematics are active loosen the selection to the medium working point
	      Int_t idbits( photons[ipho].get("id") );
	      bool hasTightPhotonId( (idbits >> 2 ) & 0x1 );
	      double gIso    = photons[ipho].getVal("gIso03");
	      double gArea   = utils::cmssw::getEffectiveArea(22,eta,3,"gIso");	      
	      double chIso   = photons[ipho].getVal("chIso03");
	      double chArea  = utils::cmssw::getEffectiveArea(22,eta,3,"chIso");
	      double nhIso   = photons[ipho].getVal("nhIso03");
	      double nhArea  = utils::cmssw::getEffectiveArea(22,eta,3,"nhIso");
	      
	      //select the photon
	      if(pt<triggerThreshold || fabs(eta)>1.4442 ) continue;
	      bool passId(true);
	      if( photons[ipho].getVal("r9")<0.9 ) passId=false;
	      if(!hasTightPhotonId) passId=false;
	      if(!passId) continue;
	      bool passIso(true);
	      passIso &= (TMath::Max(chIso-chArea*ev.rho,0.0) < 0.7); 
	      passIso &= (TMath::Max(nhIso-nhArea*ev.rho,0.0) < 0.4+0.04*pt); 
	      passIso &= (TMath::Max(gIso-gArea*ev.rho,  0.0) < 0.5+0.005*pt); 
	      if(!passIso) continue; 
	      selPhotons.push_back(photons[ipho]);
	    }
	}


      //
      // LEPTON ANALYSIS
      //
      data::PhysicsObjectCollection_t selLeptons, extraLeptons;
      for(size_t ilep=0; ilep<leptons.size(); ilep++)
	{
	  bool passKin(true),passId(true),passIso(true);
	  bool passLooseLepton(true), passSoftMuon(true);

	  int lid=leptons[ilep].get("id");

	  //apply muon corrections
	  if(abs(lid)==13)
	    {
	      passSoftMuon=false;
	      if(muCor){
		TLorentzVector p4(leptons[ilep].px(),leptons[ilep].py(),leptons[ilep].pz(),leptons[ilep].energy());
		muCor->applyPtCorrection(p4 , lid<0 ? -1 :1 );
		if(isMC) muCor->applyPtSmearing(p4, lid<0 ? -1 : 1, false);
		leptons[ilep].SetPxPyPzE(p4.Px(),p4.Py(),p4.Pz(),p4.E());
	      }
	    }

	  //no need for charge info any longer
	  lid=abs(lid);
	  TString lepStr( lid==13 ? "mu" : "e");

	  //veto nearby photon (loose electrons are many times photons...)
	  double minDRlg(9999.);
	  for(size_t ipho=0; ipho<selPhotons.size(); ipho++)
	    minDRlg=TMath::Min(minDRlg,deltaR(leptons[ilep],selPhotons[ipho]));
	  if(minDRlg<0.1) continue;
	  
	  //kinematics
	  float leta = lid==11 ? leptons[ilep].getVal("sceta") : leptons[ilep].eta();
	  if(leta> (lid==11 ? 2.5 : 2.4) )            passKin=false;
	  if(lid==11 && (leta>1.4442 && leta<1.5660)) passKin=false;
	  passLooseLepton &= passKin;
	  passSoftMuon    &= passKin;
	  if(lid==13){
	    if(leptons[ilep].pt()<10) passLooseLepton=false;
	    if(leptons[ilep].pt()<3)  passSoftMuon=false;
	  }
	  else if(lid==11){
	    if(leptons[ilep].pt()<10) passLooseLepton=false;
	  }
	  if(leptons[ilep].pt()<20) passKin=false;


	  //id
	  Int_t idbits = leptons[ilep].get("idbits");
	  if(lid==11){
	    if(leptons[ilep].getFlag("isconv"))            { passLooseLepton=false; passId=false; }
	    bool isLoose = ((idbits >> 4) & 0x1);
	    if(!isLoose)                                   passId=false; 
	    bool isVeto = ((idbits >> 4) & 0x1);
	    if(!isVeto)                                    passLooseLepton=false;
 	  }
	  else{
	    bool isLoose    = ((idbits >> 8) & 0x1);
	    if(!isLoose)                                   { passLooseLepton=false; passId=false; }
	    bool isSoft  = ((idbits >> 9) & 0x1);
	    if(!isSoft)                                    passSoftMuon=false;
	  }

	  //isolation
	  Float_t gIso    = leptons[ilep].getVal(lid==11 ? "gIso03"    : "gIso04");
	  Float_t chIso   = leptons[ilep].getVal(lid==11 ? "chIso03"   : "chIso04");
	  Float_t puchIso = leptons[ilep].getVal(lid==11 ? "puchIso03" : "puchIso04");  
	  Float_t nhIso   = leptons[ilep].getVal(lid==11 ? "nhIso03"   : "nhIso04");
	  float relIso= lid==11 ?
	    (TMath::Max(nhIso+gIso-ev.rho*utils::cmssw::getEffectiveArea(11,leptons[ilep].getVal("sceta")),Float_t(0.))+chIso)/leptons[ilep].pt() :
	    (TMath::Max(nhIso+gIso-0.5*puchIso,0.)+chIso)/leptons[ilep].pt()
	    ;
	  if(lid==11){
	    if(relIso>0.15) { 
	      passIso=false;
	      passLooseLepton=false;
	    }
	  }
	  else{
	    if(relIso>0.20){
	      passIso=false;
	      passLooseLepton=false;
	    }
	  }
	  
	  if(passId && passIso && passKin)          selLeptons.push_back(leptons[ilep]);
	  else if(passLooseLepton || passSoftMuon)  extraLeptons.push_back(leptons[ilep]);
	}
      std::sort(selLeptons.begin(),   selLeptons.end(), data::PhysicsObject_t::sortByPt);
      std::sort(extraLeptons.begin(), extraLeptons.end(), data::PhysicsObject_t::sortByPt);
      
      //
      //JET/MET ANALYSIS
      //
      //add scale/resolution uncertainties and propagate to the MET
      utils::cmssw::updateJEC(jets,jesCor,totalJESUnc,ev.rho,ev.nvtx,isMC);
      std::vector<LorentzVector> met=utils::cmssw::getMETvariations(recoMet[0],jets,selLeptons,isMC);

      //select the jets
      data::PhysicsObjectCollection_t selJets;
      int njets(0),nbtags(0);
      float mindphijmet(9999.);
      for(size_t ijet=0; ijet<jets.size(); ijet++) 
	{
	  if(jets[ijet].pt()<15 || fabs(jets[ijet].eta())>4.7 ) continue;

	  //mc truth for this jet
	  const data::PhysicsObject_t &genJet=jets[ijet].getObject("genJet");
	  TString jetType( genJet.pt()>0 ? "truejetsid" : "pujetsid" );
	  
	  //cross-clean with selected leptons and photons
	  double minDRlj(9999.),minDRlg(9999.);
          for(size_t ilep=0; ilep<selLeptons.size(); ilep++)
            minDRlj = TMath::Min( minDRlj, deltaR(jets[ijet],selLeptons[ilep]) );
	  for(size_t ipho=0; ipho<selPhotons.size(); ipho++)
	    minDRlg = TMath::Min( minDRlg, deltaR(jets[ijet],selPhotons[ipho]) );
	  if(minDRlj<0.4 || minDRlg<0.4) continue;
	  
	  //jet id
	  // float pumva=jets[ijet].getVal("puMVA");
	  Int_t idbits=jets[ijet].get("idbits");
	  bool passPFloose( ((idbits>>0) & 0x1));
	  //int puId( ( idbits >>3 ) & 0xf );
	  //bool passLoosePuId( ( puId >> 2) & 0x1);
	  int simplePuId( ( idbits >>7 ) & 0xf );
	  bool passLooseSimplePuId(  ( simplePuId >> 2) & 0x1);
	  if(jets[ijet].pt()>30)
	    {
	      mon.fillHisto(jetType,"",fabs(jets[ijet].eta()),0);
	      if(passPFloose)                        mon.fillHisto(jetType,"",fabs(jets[ijet].eta()),1);
	      if(passLooseSimplePuId)                mon.fillHisto(jetType,"",fabs(jets[ijet].eta()),2);
	      if(passPFloose && passLooseSimplePuId) mon.fillHisto(jetType,"",fabs(jets[ijet].eta()),3);
	    }
	  if(!passPFloose || !passLooseSimplePuId) continue;
	  selJets.push_back(jets[ijet]);
	  if(jets[ijet].pt()>30) {
	    njets++;
	    float dphijmet=fabs(deltaPhi(jets[ijet].phi(),met[0].phi()));
	    if(dphijmet<mindphijmet) mindphijmet=dphijmet;
	    if(fabs(jets[ijet].eta())<2.5) nbtags += (jets[ijet].getVal("jp")>0.264);
	  }
	}
      std::sort(selJets.begin(), selJets.end(), data::PhysicsObject_t::sortByPt);

      //
      // ASSIGN CHANNEL
      //
      std::vector<TString> chTags;
      int dilId(1);
      LorentzVector boson(0,0,0,0);
      if(!runPhotonSelection && selLeptons.size()==2)
	{
 	  for(size_t ilep=0; ilep<2; ilep++)
	    {
	      dilId *= selLeptons[ilep].get("id");
	      int id(abs(selLeptons[ilep].get("id")));
	      weight *= isMC ? lepEff.getLeptonEfficiency( selLeptons[ilep].pt(), selLeptons[ilep].eta(), id,  id ==11 ? "loose" : "loose" ).first : 1.0;
	      boson += selLeptons[ilep];
	    }
     
	  //check the channel
	  if( abs(dilId)==121 && eeTrigger)   chTags.push_back("ee");
	  if( abs(dilId)==169 && mumuTrigger) chTags.push_back("mumu"); 
	  if( abs(dilId)==143 && emuTrigger) chTags.push_back("emu"); 
	}
      else{
	if(hasPhotonTrigger) {
	  dilId=22;
	  chTags.push_back("ee");
	  chTags.push_back("mumu");
	  boson = selPhotons[0];
	  weight *= triggerPrescale;
	}
      }
      if(chTags.size()==0) continue;

      TString evCat=eventCategoryInst.GetCategory(selJets,boson);
      std::vector<TString> tags(1,"all");
      for(size_t ich=0; ich<chTags.size(); ich++){
	tags.push_back( chTags[ich] );
	tags.push_back( chTags[ich]+evCat );
      }

      //
      // BASELINE SELECTION
      //
      bool passMass(fabs(boson.mass()-91)<15);
      bool passQt(boson.pt()>55);
      bool passThirdLeptonVeto( selLeptons.size()==2 && extraLeptons.size()==0 );
      bool passBtags(nbtags==0);
      bool passMinDphijmet( njets==0 || mindphijmet>0.5);
      if(runPhotonSelection)
	{
	  passMass=hasPhotonTrigger;
	  passThirdLeptonVeto=(selLeptons.size()==0 && extraLeptons.size()==0);
	}
      

      mon.fillHisto("eventflow",  tags,0,weight);
      mon.fillHisto("nvtxraw",  tags,ev.nvtx,weight/puWeight);
      mon.fillHisto("nvtx",  tags,ev.nvtx,weight);
      mon.fillHisto("rho",  tags,ev.rho,weight);
      if(!runPhotonSelection){
	mon.fillHisto("leadpt",      tags,selLeptons[0].pt(),weight); 
	mon.fillHisto("trailerpt",   tags,selLeptons[1].pt(),weight); 
	mon.fillHisto("leadeta",     tags,fabs(selLeptons[0].eta()),weight); 
	mon.fillHisto("trailereta",  tags,fabs(selLeptons[1].eta()),weight); 
      }
      mon.fillHisto("zmass", tags,boson.mass(),weight); 
      mon.fillHisto("zy",    tags,fabs(boson.Rapidity()),weight); 

      if(passMass){

	mon.fillHisto("eventflow",tags, 1,weight);
	mon.fillHisto("qt",       tags, boson.pt(),weight,true); 
	mon.fillHisto("qtraw",    tags, boson.pt(),weight/triggerPrescale,true); 

	if(passQt){

	  mon.fillHisto("eventflow",tags,2,weight);
	  int nExtraLeptons((selLeptons.size()-2)+extraLeptons.size());
	  mon.fillHisto("nextraleptons",tags,nExtraLeptons,weight);
	  if(nExtraLeptons>0){
	    LorentzVector thirdLepton(selLeptons.size()>2 ?  selLeptons[1] : extraLeptons[0]);
	    double dphi=fabs(deltaPhi(thirdLepton.phi(),met[0].phi()));
	    double mt=TMath::Sqrt(2*thirdLepton.pt()*met[0].pt()*(1-TMath::Cos(dphi)));
	    mon.fillHisto("thirdleptonpt",tags,thirdLepton.pt(),weight);
	    mon.fillHisto("thirdleptoneta",tags,fabs(thirdLepton.eta()),weight);
	    mon.fillHisto("thirdleptonmt",tags,mt,weight);
	  }

	  if(passThirdLeptonVeto){
	    
	    mon.fillHisto("eventflow",tags,3,weight);
	    for(size_t ijet=0; ijet<selJets.size(); ijet++){
	      if(selJets[ijet].pt()<30 || fabs(selJets[ijet].eta())>2.5) continue;
	      float jp(selJets[ijet].getVal("jp"));
	      const data::PhysicsObject_t &genJet=jets[ijet].getObject("genJet");
	      mon.fillHisto( "jp",tags,jp,weight);

	      if(!isMC) continue;

	      int flavId=genJet.info.find("id")->second;
	      TString jetFlav("others");
	      if(abs(flavId)==5)      jetFlav="b";
	      else if(abs(flavId)==4) jetFlav="c";
	      mon.fillHisto( "jp"+jetFlav,tags,jp,weight);
	    }
	    mon.fillHisto( "nbtags",tags,nbtags,weight);
	   
	    if(passBtags){
	      mon.fillHisto("eventflow",tags,4,weight);

	      mon.fillHisto( "mindphijmet",tags,mindphijmet,weight);
	      if(passMinDphijmet){
		mon.fillHisto("eventflow",tags,5,weight);

		mon.fillHisto( "njets",tags,njets,weight);
		mon.fillHisto( "met",tags,met[0].pt(),weight);
		mon.fillHisto( "balance",tags,met[0].pt()/boson.pt(),weight);
		TVector2 met2(met[0].px(),met[0].py());
		TVector2 boson2(boson.px(), boson.py());
		double axialMet(boson2*met2); axialMet/=boson.pt();
		mon.fillHisto( "axialmet",tags,axialMet,weight);
		double mt=higgs::utils::transverseMass(boson,met[0],true);
		mon.fillHisto( "mt",tags,mt,weight);
		
		if(met[0].pt()>70)mon.fillHisto("eventflow",tags,6,weight);

		//pre-VBF control
		if(njets>=2){
		  LorentzVector dijet=selJets[0]+selJets[1];
		  float deta=fabs(selJets[0].eta()-selJets[1].eta());
		  float dphi=fabs(deltaPhi(selJets[0].phi(),selJets[1].phi()));
		  float pt1(selJets[0].pt()),pt2(selJets[1].pt());
		  mon.fillHisto( "leadjetpt",tags,pt1,weight);
		  mon.fillHisto( "trailerjetpt",tags,pt2,weight);
		  if(pt1>30 && pt2>30){
		    float eta1(selJets[0].eta()),eta2(selJets[1].eta());
		    float fwdEta( fabs(eta1)>fabs(eta2) ? eta1 : eta2);
		    float cenEta( fabs(eta1)>fabs(eta2) ? eta2 : eta1);
		    mon.fillHisto("fwdjeteta",tags,fabs(fwdEta),  weight);
		    mon.fillHisto("cenjeteta",tags,fabs(cenEta),  weight);
		    mon.fillHisto("vbfdetajj",tags,deta,        weight);
		    if(deta>4.0){
		      mon.fillHisto("vbfmjj",   tags,dijet.mass(),weight,true);
		      if(dijet.mass()>500){
			mon.fillHisto("vbfdphijj",tags,dphi,        weight);
		      }
		    }
		  }
		}
	      }
	    }
	  }
	}
      }
  }

//       //
//       // HISTOS FOR STATISTICAL ANALYSIS (include systematic variations)
//       //
//       //Fill histogram for posterior optimization, or for control regions
//       for(size_t ivar=0; ivar<nvarsToInclude; ivar++){
//         float iweight = weight;                                               //nominal
//         if(ivar==9)                         iweight *=TotalWeight_plus;        //pu up
//         if(ivar==10)                        iweight *=TotalWeight_minus;       //pu down
// 	if((ivar==11 || ivar==12) && isMC_ZZ && vvShapeUnc.size()==2)
// 	  { 
// 	    TGraph *varGr=vvShapeUnc[ivar-11];
// 	    if(varGr==0) continue;
// 	    std::vector<LorentzVector> zs;
//  	    for(Int_t ipart=0; ipart<ev.nmcparticles; ipart++)
// 	      {
// 		if(ev.mc_id[ipart]!=23) continue;
// 		zs.push_back( LorentzVector(ev.mc_px[ipart],ev.mc_py[ipart],ev.mc_pz[ipart],ev.mc_en[ipart]) );
// 	      }
// 	    if(zs.size()==2)
// 	      {
// 		LorentzVector zz=zs[0]+zs[1];
// 		iweight *= varGr->Eval(zz.pt());
// 	      }
// 	  }
//         if(ivar<=14 && ivar>=11 && isMC_GG)                                  //ren/fact scales
// 	  {
// 	    float hptReweight = ev.hptWeights[ivar-10];
// 	    if(ev.hptWeights[0]==0) hptReweight=0;
// 	    else                    hptReweight/=ev.hptWeights[0];
// 	    iweight *= hptReweight;

// 	    //these are irrelevant systematics
// 	    continue;
// 	  }
// 	if((ivar==17 || ivar==18) && isMC_GG)                                //shape unc
// 	  {
// 	    float shapeReWeight = lShapeWeights[ivar-16];
// 	    if(lShapeWeights[0]==0) shapeReWeight=0;
// 	    else                    shapeReWeight /= lShapeWeights[0];
// 	    iweight                 *= shapeReWeight;
// 	  }

// 	//recompute MET/MT if JES/JER was varied
// 	LorentzVector zvv    = zvvs[ivar>8 ? 0 : ivar];
// 	PhysicsObjectJetCollection &varJets=variedAJets[ivar>4 ? 0  : ivar];
// 	PhysicsObjectJetCollection tightVarJets;
// 	LorentzVector clusteredMetP4(zll); clusteredMetP4 *= -1;
// 	bool passLocalBveto(passBveto);
// 	for(size_t ijet=0; ijet<varJets.size(); ijet++){
// 	  if(!hasObjectId(varJets[ijet].pid,JETID_LOOSE) || fabs(varJets[ijet].eta())>4.7) continue;
// 	  clusteredMetP4 -= varJets[ijet];
// 	  if( hasObjectId(varJets[ijet].pid,JETID_CUTBASED_LOOSE) ) tightVarJets.push_back( varJets[ijet] );
// 	  if(varJets[ijet].pt()<30 || fabs(varJets[ijet].eta())>2.5) continue;
// 	  //FIXME
// 	  //if(ivar==15)     {
// 	  //btsfutil.modifyBTagsWithSF(passLocalBveto, varJets[ijet].flavid, 0.98, 0.841*1.02, 1.21, 0.137*1.11);
// 	  //}
// 	  //else if(ivar==16) {
// 	  // btsfutil.modifyBTagsWithSF(passLocalBveto, varJets[ijet].flavid, 0.98, 0.841*0.98, 1.21, 0.137*0.89);
// 	  //}
// 	}
// 	bool passPreselection                 (passZmass && passZpt && pass3dLeptonVeto && passDphijmet && passLocalBveto && passLMetVeto);
// 	bool passPreselectionMbvetoMzmass     (             passZpt && pass3dLeptonVeto && passDphijmet                   && passLMetVeto);          
// 	bool passPreselectionMdphi            (passZmass && passZpt && pass3dLeptonVeto                 && passLocalBveto && passLMetVeto);
// 	bool passPreselectionMbvetoMzmassMdphi(             passZpt && pass3dLeptonVeto                                   && passLMetVeto);          
	
// 	float mt = METUtils::transverseMass(zll,zvv,true);
// 	LorentzVector nullP4(0,0,0,0);
// 	LorentzVector redMet = METUtils::redMET(METUtils::INDEPENDENTLYMINIMIZED, zll, 0, nullP4, 0, clusteredMetP4, zvv,true);

// 	//re-assign the event category if jets were varied
// 	int eventSubCat  = eventCategoryInst.Get(phys,&tightVarJets);
// 	TString tag_subcat = eventCategoryInst.GetLabel(eventSubCat);
// 	tags_full.clear();
// 	tags_full.push_back(tag_cat);
// 	tags_full.push_back(tag_cat+tag_subcat);
// 	//if(tag_subcat!="vbf") tags_full.push_back(tag_cat + "novbf");
//         if(tag_subcat=="eq1jets" || tag_subcat=="geq2jets")tags_full.push_back(tag_cat + "geq1jets");
//         //if(tag_subcat=="geq2jets" || tag_subcat=="vbf")tags_full.push_back(tag_cat + "geq2jetsInc");
//         //if(tag_cat=="mumu" || tag_cat=="ee")tags_full.push_back(string("ll")+tag_subcat);
//         if(tag_cat=="mumu" || tag_cat=="ee"){
// 	  tags_full.push_back(string("ll")+tag_subcat);  
// 	  if(tag_subcat=="eq1jets" || tag_subcat=="geq2jets")tags_full.push_back(string("ll")+string("geq1jets"));   
// 	}
// 	//        //remove sub VBF category to make it faster
// 	//        if(tag_subcat=="vbf"){
// 	//	  TString tag_subcatVBF = tag_subcat;
// 	//	  if(fabs(tightVarJets[0].eta())<2.1 && fabs(tightVarJets[1].eta())<2.1)      { tag_subcatVBF+="2"; }
// 	//	  else if(fabs(tightVarJets[0].eta())<2.1 || fabs(tightVarJets[1].eta())<2.1) { tag_subcatVBF+="1"; }
// 	//	  else                                                                        { tag_subcatVBF+="0"; }
// 	//	  tags_full.push_back(tag_cat+tag_subcatVBF);
// 	//	  if(tag_cat=="mumu" || tag_cat=="ee")                                        { tags_full.push_back(string("ll")+tag_subcatVBF); }
// 	//        }
// 	if(passPreselection && zvv.pt()>30) mon.fillHisto("mtvar"+varNames[ivar],tags_full,mt,iweight);
	
// 	/*
// 	//DEBUG
// 	if(ivar==0 && outTxtFile && tag_subcat=="vbf" && zvv.pt()>70 && passPreselection){
// 	fprintf(outTxtFile,"X----------------------------\nCat: %s - %s\n",tag_cat.Data(),tag_subcat.Data());
// 	fprintf(outTxtFile,"inputFile = %s\n",url.Data());
// 	fprintf(outTxtFile,"run/lumi/event = %i/%i/%i\n",ev.run, ev.lumi, ev.event);
// 	fprintf(outTxtFile,"mt = %f met = %f -redMet = %f\n",mt, zvv.pt(), redMet.pt());
// 	}
	
// 	if(ivar==0 && outTxtFile && tag_subcat=="geq2jets" && zvv.pt()>100 && mt<250 && passPreselection){
// 	fprintf(outTxtFile,"DEBUG----------------------------\nCat: %s - %s\n",tag_cat.Data(),tag_subcat.Data());
// 	fprintf(outTxtFile,"subcat = %s inputFile = %s\n",tag_subcat.Data(), url.Data());
// 	fprintf(outTxtFile,"run/lumi/event = %i/%i/%i\n",ev.run, ev.lumi, ev.event);
// 	fprintf(outTxtFile,"mt = %f met = %f -redMet = %f\n",mt, zvv.pt(), redMet.pt());
// 	fprintf(outTxtFile,"nvtx = %i rho=%f rho25 = %f\n",ev.nvtx,ev.rho, ev.rho25Neut);
// 	fprintf(outTxtFile,"zll  pt=%f mass=%f eta=%f phi=%f\n",zll.pt(), zll.mass(), zll.eta(), zll.phi());
// 	for(unsigned int j=0;j<phys.ajets.size();j++){
// 	fprintf(outTxtFile,"jet %i  pt=%f eta=%f phi=%f\n",j, phys.ajets[j].pt(), phys.ajets[j].eta(), phys.ajets[j].phi());
// 	}
// 	}
// 	*/
	
// 	//fill shapes
// 	for(unsigned int index=0;index<optim_Cuts1_met.size();index++){             
// 	  //              if(redMet.pt()>optim_Cuts1_met[index]){
// 	  //                if(passPreselection                                                         )   mon.fillHisto(TString("mt_redMet_shapes")+varNames[ivar],tags_full,index, mt,iweight);
// 	  //                if(passPreselectionMbvetoMzmass && passZmass         && passLocalBveto      )   mon.fillHisto("mt_redMet_shapes_NRBctrl"+varNames[ivar],tags_full,index,0,iweight);
// 	  //                if(passPreselectionMbvetoMzmass && isZsideBand       && passLocalBveto      )   mon.fillHisto("mt_redMet_shapes_NRBctrl"+varNames[ivar],tags_full,index,1,iweight);
// 	  //                if(passPreselectionMbvetoMzmass && isZsideBandPlus   && passLocalBveto      )   mon.fillHisto("mt_redMet_shapes_NRBctrl"+varNames[ivar],tags_full,index,2,iweight);
// 	  //                if(passPreselectionMbvetoMzmass && passZmass         && !passLocalBveto     )   mon.fillHisto("mt_redMet_shapes_NRBctrl"+varNames[ivar],tags_full,index,3,iweight);
// 	  //                if(passPreselectionMbvetoMzmass && isZsideBand       && !passLocalBveto     )   mon.fillHisto("mt_redMet_shapes_NRBctrl"+varNames[ivar],tags_full,index,4,iweight);
// 	  //                if(passPreselectionMbvetoMzmass && isZsideBandPlus   && !passLocalBveto     )   mon.fillHisto("mt_redMet_shapes_NRBctrl"+varNames[ivar],tags_full,index,5,iweight);
// 	  //              }
	  
// 	  //NARROW RESONANCE ANALYSIS
// 	  if(zvv.pt()>optim_Cuts1_met[index]){
// 	    for(unsigned int nri=0;nri<NRparams.size();nri++){
	      
// 	      float nrweight=iweight*NRweights[nri];
// 	      if(nri>0)
// 		{
// 		  nrweight=iweight*NRweights[nri];
// 		  if(lShapeWeights[0]==0) nrweight=0;
// 		  else                    nrweight/=lShapeWeights[0];
// 		}

// 	      if(passPreselection                                                         )   mon.fillHisto(TString("mt_shapes")+NRsuffix[nri]+varNames[ivar],tags_full,index, mt,nrweight);
// 	      if(passPreselection                                                         )   mon.fillHisto(TString("met_shapes")+NRsuffix[nri]+varNames[ivar],tags_full,index, zvv.pt(),nrweight);
// 	      if(passPreselectionMbvetoMzmass && passZmass         && passLocalBveto      )   mon.fillHisto("mt_shapes_NRBctrl"+NRsuffix[nri]+varNames[ivar],tags_full,index,0,nrweight);
// 	      if(passPreselectionMbvetoMzmass && isZsideBand       && passLocalBveto      )   mon.fillHisto("mt_shapes_NRBctrl"+NRsuffix[nri]+varNames[ivar],tags_full,index,1,nrweight);
// 	      if(passPreselectionMbvetoMzmass && isZsideBandPlus   && passLocalBveto      )   mon.fillHisto("mt_shapes_NRBctrl"+NRsuffix[nri]+varNames[ivar],tags_full,index,2,nrweight);
// 	      if(passPreselectionMbvetoMzmass && passZmass         && !passLocalBveto     )   mon.fillHisto("mt_shapes_NRBctrl"+NRsuffix[nri]+varNames[ivar],tags_full,index,3,nrweight);
// 	      if(passPreselectionMbvetoMzmass && isZsideBand       && !passLocalBveto     )   mon.fillHisto("mt_shapes_NRBctrl"+NRsuffix[nri]+varNames[ivar],tags_full,index,4,nrweight);
// 	      if(passPreselectionMbvetoMzmass && isZsideBandPlus   && !passLocalBveto     )   mon.fillHisto("mt_shapes_NRBctrl"+NRsuffix[nri]+varNames[ivar],tags_full,index,5,nrweight);
	      
// 	      //unrolled in dphi(jet,met)
// //	      float unrolledmt(mt); 
// //	      if(mt>1150)      mt=1140;
// //	      if(passDphijmet) unrolledmt += 1000;
// //	      if(passPreselectionMdphi                                                         )   mon.fillHisto(TString("mt_shapes_unroll")+NRsuffix[nri]+varNames[ivar],tags_full,index, unrolledmt,nrweight);
// //	      if(passPreselectionMbvetoMzmassMdphi && passZmass         && passLocalBveto      )   mon.fillHisto("mt_shapes_unroll_NRBctrl"+NRsuffix[nri]+varNames[ivar],tags_full,index,0,nrweight);
// //	      if(passPreselectionMbvetoMzmassMdphi && isZsideBand       && passLocalBveto      )   mon.fillHisto("mt_shapes_unroll_NRBctrl"+NRsuffix[nri]+varNames[ivar],tags_full,index,1,nrweight);
// //	      if(passPreselectionMbvetoMzmassMdphi && isZsideBandPlus   && passLocalBveto      )   mon.fillHisto("mt_shapes_unroll_NRBctrl"+NRsuffix[nri]+varNames[ivar],tags_full,index,2,nrweight);
// //	      if(passPreselectionMbvetoMzmassMdphi && passZmass         && !passLocalBveto     )   mon.fillHisto("mt_shapes_unroll_NRBctrl"+NRsuffix[nri]+varNames[ivar],tags_full,index,3,nrweight);
// //	      if(passPreselectionMbvetoMzmassMdphi && isZsideBand       && !passLocalBveto     )   mon.fillHisto("mt_shapes_unroll_NRBctrl"+NRsuffix[nri]+varNames[ivar],tags_full,index,4,nrweight);
// //	      if(passPreselectionMbvetoMzmassMdphi && isZsideBandPlus   && !passLocalBveto     )   mon.fillHisto("mt_shapes_unroll_NRBctrl"+NRsuffix[nri]+varNames[ivar],tags_full,index,5,nrweight);
// 	    }
// 	  }         
// 	}
	
// 	for(unsigned int index=0;index<optim_Cuts2_met.size();index++){
// 	  if(varJets.size()>=2 && zvv.pt()>optim_Cuts2_met[index] && varJets[0].pt()>optim_Cuts2_vbfJpt[index] && varJets[1].pt()>optim_Cuts2_vbfJpt[index] &&  fabs(varJets[0].eta()-varJets[1].eta())>optim_Cuts2_vbfdeta[index]  ){
// 	    if(passPreselection                                                         )   mon.fillHisto("VBFmt_shapes"        +varNames[ivar],tags_full,index, (varJets[0] + varJets[1]).M(),iweight);
// 	    if(passPreselectionMbvetoMzmass && passZmass         && passLocalBveto      )   mon.fillHisto("VBFmt_shapes_NRBctrl"+varNames[ivar],tags_full,index,0,iweight);
// 	    if(passPreselectionMbvetoMzmass && isZsideBand       && passLocalBveto      )   mon.fillHisto("VBFmt_shapes_NRBctrl"+varNames[ivar],tags_full,index,1,iweight);
// 	    if(passPreselectionMbvetoMzmass && isZsideBandPlus   && passLocalBveto      )   mon.fillHisto("VBFmt_shapes_NRBctrl"+varNames[ivar],tags_full,index,2,iweight);
// 	    if(passPreselectionMbvetoMzmass && passZmass         && !passLocalBveto     )   mon.fillHisto("VBFmt_shapes_NRBctrl"+varNames[ivar],tags_full,index,3,iweight);
// 	    if(passPreselectionMbvetoMzmass && isZsideBand       && !passLocalBveto     )   mon.fillHisto("VBFmt_shapes_NRBctrl"+varNames[ivar],tags_full,index,4,iweight);
// 	    if(passPreselectionMbvetoMzmass && isZsideBandPlus   && !passLocalBveto     )   mon.fillHisto("VBFmt_shapes_NRBctrl"+varNames[ivar],tags_full,index,5,iweight);
// 	  }
// 	}         
//       }
//       //cout << endl;
    
  printf("\n"); 
  file->Close();
  
  //##############################################
  //########     SAVING HISTO TO FILE     ########
  //##############################################
  //save control plots to file
  outUrl += "/";
  outUrl += outFileUrl + ".root";
  printf("Results save in %s\n", outUrl.Data());
  
  //save all to the file
  TFile *ofile=TFile::Open(outUrl, "recreate");
  mon.Write();
  ofile->Close();

  if(outTxtFile)fclose(outTxtFile);
}  





