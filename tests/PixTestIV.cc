#include <stdlib.h>    
#include <algorithm>   
#include <iostream>
#include <fstream>

#include <TH1.h>
#include <TMath.h>
#include <TTime.h>
#include <TPad.h>

#include "PixTestIV.hh"
#include "log.h"
#include "helper.h"
#ifdef BUILD_HV
#include "hvsupply.h"
#endif

using namespace pxar;
using namespace std;

ClassImp(PixTestIV)

// ----------------------------------------------------------------------
PixTestIV::PixTestIV(PixSetup *a, string name) : PixTest(a, name), fParVoltageMax(150), fParVoltageStep(5), fParDelay(1), fStop(false), fParPort("") {
  PixTest::init();
  init();
}

// ----------------------------------------------------------------------
PixTestIV::PixTestIV() : PixTest() {}


// ----------------------------------------------------------------------
bool PixTestIV::setParameter(string parName, string sval) {
  bool found(false);

  std::transform(parName.begin(), parName.end(), parName.begin(), ::tolower);
  for (unsigned int i = 0; i < fParameters.size(); ++i) {
    if (fParameters[i].first == parName) {
      found = true;
      sval.erase(remove(sval.begin(), sval.end(), ' '), sval.end());
      if(!parName.compare("voltagemin")) {
        fParVoltageMin = atof(sval.c_str());
        setToolTips();
      }
      if(!parName.compare("voltagemax")) {
        fParVoltageMax = atof(sval.c_str());
        setToolTips();
      }
      if(!parName.compare("voltagestep")) {
        fParVoltageStep = atof(sval.c_str());
        setToolTips();
      }
      if(!parName.compare("delay")) {
        fParDelay = atof(sval.c_str());
        setToolTips();
      }
      if(!parName.compare("port")) {
        fParPort = sval;
        setToolTips();
      }
      break;
    }
  }
  return found;
}

// ----------------------------------------------------------------------
void PixTestIV::init() {
  fDirectory = gFile->GetDirectory( fName.c_str() );
  if( !fDirectory )
    fDirectory = gFile->mkdir( fName.c_str() );
  fDirectory->cd();
}

// ----------------------------------------------------------------------
void PixTestIV::bookHist(string /*name*/) {
  fDirectory->cd();
}

// ----------------------------------------------------------------------
void PixTestIV::stop() {
  fStop = true;
  LOG(logINFO) << "Stop pressed. Ending test.";
}


// ----------------------------------------------------------------------
PixTestIV::~PixTestIV() {
  LOG(logDEBUG) << "PixTestIV dtor";
  if (fPixSetup->doMoreWebCloning()) output4moreweb();
}

// ----------------------------------------------------------------------
void PixTestIV::doTest() {

#ifndef BUILD_HV
  LOG(logERROR) << "Not built with HV supply support.";
  return;
#else

  fDirectory->cd();
  
  int numMeasurements = (fParVoltageMax - fParVoltageMin)/fParVoltageStep
  
  TH1D *h1(0);
  h1 = bookTH1D("IVcurve", "IV curve", num_measurements, 0, fParVoltageMax+1);
  h1->SetMinimum(1.e-2);
  h1->SetMarkerStyle(20);
  h1->SetMarkerSize(1.3);
  h1->SetStats(0.);
  setTitles(h1, "-U [V]", "-I [uA]");

  unsigned int timeStamps[numMeasurements];
  double voltageSettings[numMeasurements];
  double voltageMeasurements[numMeasurements];
  double currentMeasurements[numMeasurements];
  
  for(unsigned int i = 0; i < numMeasurements;i++) {
    voltageSettings[i] = fParVoltageMin + i*fParVoltageStep;
  }
  PixTest::update();
  gPad->SetLogy(true);
  
  if(hv->supportSweep()){
    hv->sweep(voltageSettings, 
              timeStamps, 
              voltageMeasurements, 
              currentMeasurements);
  } else{
    
    
  }
  
  //////////////////////////////////////////
  
  
  map<int, uint32_t> ts;
  map<int, double> vm;

  PixTest::update();

  if(gPad) gPad->SetLogy(1);

  if(hv->supportSweep()){
    hv->sweep(fParVoltageMin, fParVoltageMin, fParVoltageStep);
  }
  else{
    int tripVoltage = -1;
    TTimeStamp startTs;
    // -- loop over voltage:
    float voltMeasured(-1.), amps(-1.);
    for(float voltSet = fParVoltageMin; voltSet <= fParVoltageMax; voltSet += fParVoltageStep) {    
      hv->setVoltage(voltSet);
      mDelay(fParDelay*1000); 
      // -- get within 0.5V of specified voltage. Try at most 5 times.
      int ntry(0);
      while (ntry < 5) {
        gSystem->ProcessEvents();
        if (fStop) break;
        hv->getVoltageCurrent(voltMeasured, amps);
        amps *= 1.e6;
        if (TMath::Abs(voltSet + voltMeasured) < 0.5) break; // assume that voltMeasured is negative!
        ++ntry;
      }
      if (fStop) break;

      fTimeStamp->Set();
      ts.insert(make_pair(static_cast<uint32_t>(TMath::Abs(voltSet)), fTimeStamp->GetTimeSpec().tv_sec));
      vm.insert(make_pair(static_cast<uint32_t>(TMath::Abs(voltSet)), voltMeasured));

      if (hv->tripped() || ((amps<-99.) && (voltMeasured !=0.))) {
        LOG(logCRITICAL) << "HV supply tripped, aborting IV test"; 
        tripVoltage = voltSet;
        break;
      }
      LOG(logINFO) << Form("V = %4d (meas: %+7.2f) I = %4.2e uA (ntry = %d) %ld", 
                           -voltSet, voltMeasured, amps, ntry, fTimeStamp->GetTimeSpec().tv_sec);
      if (TMath::Abs(amps) > 0.) {
        h1->Fill(TMath::Abs(voltSet), TMath::Abs(amps));
      }
      h1->Draw("p");
      PixTest::update();
    }

    // -- ramp down voltage
    int vstep(50);
    for (int voltSet = fParVoltageMax - vstep; voltSet > 100; voltSet -= vstep) {
      LOG(logDEBUG) << "ramping down voltage, Vset = " << voltSet;
      hv->setVoltage(voltSet);
    }
    delete hv;
}

  fHistList.push_back(h1);
  fDisplayedHist = find(fHistList.begin(), fHistList.end(), h1);
  h1->Draw("p");
  PixTest::update();

  ofstream OutputFile;
  OutputFile.open(Form("%s/ivCurve.log", fPixSetup->getConfigParameters()->getDirectory().c_str())); 
  OutputFile << "# IV test from "   << startTs.AsString("l") << endl;
  OutputFile << "#voltage(V)\tcurrent(A)\ttimestamp" << endl;

  for (int voltSet = fParVoltageMin; voltSet <= fParVoltageMax; voltSet += fParVoltageStep) {
    if (tripVoltage > -1 && voltSet >= tripVoltage) break;
    OutputFile << Form("%+8.3f\t%+e\t%ld", 
               static_cast<double>(vm[voltSet]), 
               -1.e-6*h1->GetBinContent(h1->FindBin(voltSet)), 
               static_cast<unsigned long>(ts[voltSet])
               ) << endl; 
  }
  OutputFile.close();
#endif
  LOG(logINFO) << "PixTestIV::doTest() done ";
}

void PixTestIV::sweep(){
  
  
  
}
