/*
------------------------------------------------------------------

This file is part of a plugin for the Open Ephys GUI
Copyright (C) 2017 Translational NeuroEngineering Laboratory, MGH

------------------------------------------------------------------

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/
/*
This is based heavily on the crossing detector third party plugin
First, simply filtering the data before thresholding
*/

#include "SeizureDetector.h"
#include "SeizureDetectorEditor.h"
#include "boostAcc/boost/accumulators/accumulators.hpp"
#include "boostAcc/boost/accumulators/statistics.hpp"
#include "boostAcc/boost/accumulators/statistics/rolling_mean.hpp"




SeizureDetector::SeizureDetector()
    : GenericProcessor  ("Seizure Detector")
    , inputChan         (0)
	, rollDur           (1000)
	, alphaLow          (6.0f)
	, alphaHigh         (9.0f)
	, alphaGain         (1.0f)
	, betaLow           (13.0f)
	, betaHigh          (18.0f)
	, betaGain          (1.0f)
	, deltaLow          (1.0f)
	, deltaHigh         (4.0f)
	, deltaGain         (1.0f)
{
    setProcessorType(PROCESSOR_TYPE_FILTER);

	


}

SeizureDetector::~SeizureDetector() {}

AudioProcessorEditor* SeizureDetector::createEditor()
{
    editor = new SeizureDetectorEditor(this);
    return editor;
}

void SeizureDetector::createEventChannels()
{
    // add detection event channel
    const DataChannel* in = getDataChannel(inputChan);
    float sampleRate = in ? in->getSampleRate() : CoreServices::getGlobalSampleRate();

	//create accumulator and buffers for filtering
	//snuck in with create event channels because it only 

	deltaAcc acc(bt::rolling_window::window_size = sampleRate);

	scratchBuffer = AudioSampleBuffer(5, sampleRate); // 5-dimensional buffer to hold band-filtered, averaged data


	setFilterParameters();
	setRollingWindowParameters();

}

//void SeizureDetector::updateSettings()
//{
	
//}

void SeizureDetector::setRollingWindowParameters()
{
	//set rolling buffer size
	int sampRate = dataChannelArray[inputChan]->getSampleRate();
	int buffSize = sampRate*rollDur / 1000;
	rollBuffer = AudioSampleBuffer(2, buffSize);    // buffer for rolling average data

}

void SeizureDetector::setFilterParameters()
{


	//design 3 filters with similar properties

	int sampRate = dataChannelArray[inputChan]->getSampleRate();

	for (int n = 0; n < 3; n = n + 1)
	{
		filters.add(new Dsp::SmoothedFilterDesign
			<Dsp::Butterworth::Design::BandPass    // design type
			<2>,                                   // order
			1,                                     // number of channels (must be const)
			Dsp::DirectFormII>(1));               // realization
	}
	//alpha (fundamental) frequency parameters
	//alphaHigh = 9;
	//alphaLow = 6;
	Dsp::Params alphaParams;
	alphaParams[0] = sampRate; // sample rate
	alphaParams[1] = 2;                          // order
	alphaParams[2] = (alphaHigh + alphaLow) / 2;     // center frequency
	alphaParams[3] = alphaHigh - alphaLow;           // bandwidth

	filters[0]->setParams(alphaParams);
	
	//beta (harmonic) frequency paramteters
	//betaHigh = 18;
	//betaLow = 13;
	Dsp::Params betaParams;
	betaParams[0] = sampRate;
	betaParams[1] = 2;
	betaParams[2] = (betaHigh + betaLow) / 2;
	betaParams[3] = betaHigh - betaLow;

	filters[1]->setParams(betaParams);

	//delta frequency parameters
	//deltaHigh = 4;
	//deltaLow = 1;
	Dsp::Params deltaParams;
	deltaParams[0] = sampRate;
	deltaParams[1] = 2;
	deltaParams[2] = (deltaHigh + deltaLow) / 2;
	deltaParams[3] = deltaHigh - deltaLow;

	filters[2]->setParams(deltaParams);


}

void SeizureDetector::process(AudioSampleBuffer& continuousBuffer)
{
    // state to keep constant during each call
    int currChan = inputChan;

    if (currChan < 0 || currChan >= continuousBuffer.getNumChannels()) // (shouldn't really happen)
        return;

    int nSamples = getNumSamples(currChan);
    const float* rp = continuousBuffer.getReadPointer(currChan);

	//get adjacent channel numbers to display raw data, pre-averaged signal
	int preAvgChan;
	int rawChan;

	if (currChan < (continuousBuffer.getNumChannels() - 2))
	{
		rawChan = currChan + 2;
		preAvgChan = currChan + 1;
	}
	else if (currChan < (continuousBuffer.getNumChannels() - 1))
	{
		rawChan = currChan - 1;
		preAvgChan = currChan + 1;
	}
	else
	{
		preAvgChan = currChan - 1;
		rawChan = currChan - 2;
	}

	//copy channel to scractchBuffer channels for processing

	for (int n = 0; n < 3; n = n + 1)
	{
		 scratchBuffer.copyFrom(n,
	                   	       	0,
		                      	continuousBuffer,
			                    currChan,
			                    0,
			                    nSamples);
	}

	//filter channel in alpha band and set gain
	float* ptrA = scratchBuffer.getWritePointer(0);
	filters[0]->process(nSamples, &ptrA);
	//const float alphaGain = 7.0f;
	scratchBuffer.applyGain(0,
		                    0,
		                    nSamples,
	                        alphaGain);

	//filter copied channel in beta band and set gain
	float* ptrB = scratchBuffer.getWritePointer(1);
	filters[1]->process(nSamples, &ptrB);
	scratchBuffer.applyGain(1,
		                    0,
		                    nSamples,
		                    betaGain);

	//filter copied channel in delta band and set gain
	float* ptrD = scratchBuffer.getWritePointer(2);
	filters[2]->process(nSamples, &ptrD);
	scratchBuffer.applyGain(2,
			                0,
		                    nSamples,
		                    deltaGain);

	//copy raw data from input/trigger channel to adjacent channel for viewing
	continuousBuffer.copyFrom(rawChan,
		                      0,
		                      continuousBuffer,
		                      currChan,
		                      0,
		                      nSamples);

	//add alpha, beta, and delta channels together in continuous buffer, adding gain to the beta 
	//and delta bands
	continuousBuffer.copyFrom(currChan,
	  	                      0,
		                      scratchBuffer,
	                          0,
		                      0,
		                      nSamples);

	continuousBuffer.addFrom(currChan,			//dest channel
		                     0,                 //dest start sample
		                     scratchBuffer,  //source buffer
		                     1,          //source channel
		                     0,                 //source start sample
		                     nSamples);          //num samples
		                     //betaGain);             //gain

	continuousBuffer.addFrom(currChan,
		                     0,
		                     scratchBuffer,
		                     2,
		                     0,
		                     nSamples);
		                     //deltaGain);

	//show unaveraged trigger signal on output channel adjacent to 
	//input/triggering channel

	continuousBuffer.copyFrom(preAvgChan,
							  0,
		                      continuousBuffer,
		                      currChan,
		                      0,
		                      nSamples);
	

	//apply a 1s rolling average do delta band
	//initialize constants
	int sampRate = dataChannelArray[currChan]->getSampleRate();
	int rollSamples = rollBuffer.getNumSamples();
	int rollAdd = rollSamples - nSamples;
	
	//initialize accumulator with 1 second rolling window
	deltaAcc acc(bt::rolling_window::window_size = rollSamples);
	float rollM;

	//push previous 1 second of differentiated data into the accumulator
	for (int i = 0; i < rollSamples-1; i++)
	{
		acc(std::fabs(rollBuffer.getSample(0, i+1)-rollBuffer.getSample(0,i)));
		//acc(rollBuffer.getSample(0, i));
	}

	//push new data into accumulator and get rolling average
	//acc(continuousBuffer.getSample(0, 0) - rollBuffer.getSample(0,sampRate));
	rollM = boost::accumulators::rolling_mean(acc);
	scratchBuffer.setSample(3, 0, rollM);
	//acc(rollBuffer.getSample(0, sampRate));
	for (int i = 0; i < nSamples-1; i++)
	{
		acc(std::fabs(continuousBuffer.getSample(currChan, i + 1) - continuousBuffer.getSample(currChan, i)));
		rollM = boost::accumulators::rolling_mean(acc);
		//put the rolling mean into channel 3(4) of the scratch buffer
		scratchBuffer.setSample(3, i+1, rollM);
	}


	//update rolling buffer, making sure to put new samples at 
	//the end for smooth rolling!

	rollBuffer.copyFrom(0,
		0,
		rollBuffer,
		0,
		nSamples,
		rollAdd);

	rollBuffer.copyFrom(0,
		rollAdd,
		continuousBuffer,
		currChan,
		0,
		nSamples);


	//temporarily add gain to output signal so that its units are more useful
	scratchBuffer.applyGain(3, 0, nSamples, 100);

	//overwrite the triggering channel with 1s averaged data
	continuousBuffer.copyFrom(currChan,
		                      0,
		                      scratchBuffer,
		                      3,
		                      0,
		                      nSamples);

	scratchBuffer.applyGain(3, 0, nSamples, 1);




	//show raw delta on output channel 5
	//continuousBuffer.copyFrom(4,
	//	                     0,
	//	                     scratchBuffer,
	//	                     2,
	//	                     0,
	//	                     nSamples);

	//continuousBuffer.applyGain(1);

	//end filtering code

    
}

// all new values should be validated before this function is called!
void SeizureDetector::setParameter(int parameterIndex, float newValue)
{
    switch (parameterIndex)
    {
    case pInputChan:
        if (getNumInputs() > newValue)
            inputChan = static_cast<int>(newValue);
        break;
		
	case pRollDur:
		rollDur = newValue;
		setRollingWindowParameters();
		break;

	case pAlphaLow:
		alphaLow = newValue;
		setFilterParameters();
		break;

	case pAlphaHigh:
		alphaHigh = newValue;
		setFilterParameters();
		break;

	case pAlphaGain:
		alphaGain = newValue;
		break;

	case pBetaLow:
		betaLow = newValue;
		setFilterParameters();
		break;
	
	case pBetaHigh:
		betaHigh = newValue;
		setFilterParameters();
		break;

	case pBetaGain:
		betaGain = newValue;
		break;

	case pDeltaLow:
		deltaLow = newValue;
		setFilterParameters();
		break;
		
	case pDeltaHigh:
		deltaHigh = newValue;
		setFilterParameters();
		break;

	case pDeltaGain:
		deltaGain = newValue;
		break;
    }
}

bool SeizureDetector::disable()
{

    return true;
}




