// Copyright CERN and copyright holders of ALICE O2. This software is
// distributed under the terms of the GNU General Public License v3 (GPL
// Version 3), copied verbatim in the file "COPYING".
//
// See http://alice-o2.web.cern.ch/license for full licensing information.
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.

#include "SubTimeFrameSenderDevice.h"

#include <SubTimeFrameDataModel.h>
#include <SubTimeFrameVisitors.h>

#include <options/FairMQProgOptions.h>
#include <FairMQLogger.h>

#include <chrono>
#include <thread>

namespace o2
{
namespace DataDistribution
{

using namespace std::chrono_literals;

StfSenderDevice::StfSenderDevice()
  : DataDistDevice(),
    IFifoPipeline(ePipelineSize),
    mFileSink(*this, *this, eFileSinkIn, eFileSinkOut),
    mOutputHandler(*this)
{
}

StfSenderDevice::~StfSenderDevice()
{
}

void StfSenderDevice::InitTask()
{
  mInputChannelName = GetConfig()->GetValue<std::string>(OptionKeyInputChannelName);
  mStandalone = GetConfig()->GetValue<bool>(OptionKeyStandalone);
  mMaxStfsInPipeline = GetConfig()->GetValue<std::int64_t>(OptionKeyMaxBufferedStfs);
  mMaxConcurrentSends = GetConfig()->GetValue<std::int64_t>(OptionKeyMaxConcurrentSends);
  mOutputChannelName = GetConfig()->GetValue<std::string>(OptionKeyOutputChannelName);
  mEpnNodeCount = GetConfig()->GetValue<std::uint32_t>(OptionKeyEpnNodeCount);
  mBuildHistograms = GetConfig()->GetValue<bool>(OptionKeyGui);

  // equivalent options
  if (mEpnNodeCount == 0 || mStandalone) {
    mEpnNodeCount = 0;
    mStandalone = true;
  }

  // Buffering limitation
  if (mMaxStfsInPipeline > 0) {
    if (mMaxStfsInPipeline < 4) {
      mMaxStfsInPipeline = 4;
      LOG(INFO) << "Max buffered SubTimeFrames limit increased to: " << mMaxStfsInPipeline;
    }
    mPipelineLimit = true;
    LOG(WARN) << "Max buffered SubTimeFrames limit is set to " << mMaxStfsInPipeline
              << ". Consider increasing it if data loss occurs.";
  } else {
    mPipelineLimit = false;
    LOG(INFO) << "Not imposing limits on number of buffered SubTimeFrames. "
                 "Possibility of creating back-pressure";
  }

  // File sink
  if (!mFileSink.loadVerifyConfig(*(this->GetConfig())))
    exit(-1);

  // check if any outputs enabled
  if (mStandalone && !mFileSink.enabled()) {
    LOG(WARNING) << "Running in standalone mode and with STF file sink disabled. "
                    "Data will be lost.";
  }

  // update number of send slots
  mOutputHandler.setMaxConcurrentSends(mMaxConcurrentSends);

  // gui thread
  if (mBuildHistograms) {
    mGui = std::make_unique<RootGui>("STFSender", "STF Sender", 400, 400);
    mGui->Canvas().Divide(1, 1);
    mGuiThread = std::thread(&StfSenderDevice::GuiThread, this);
  }
}

void StfSenderDevice::PreRun()
{
  // Start output handler
  // NOTE: required even in standalone operation
  mOutputHandler.start(mEpnNodeCount);

  // start file sink
  if (mFileSink.enabled()) {
    mFileSink.start();
  }
  // start the receiver thread
  mReceiverThread = std::thread(&StfSenderDevice::StfReceiverThread, this);
}

void StfSenderDevice::PostRun()
{
  // Stop the pipeline
  stopPipeline();

  // stop the receiver thread
  if (mReceiverThread.joinable()) {
    mReceiverThread.join();
  }

  // stop file sink
  if (mFileSink.enabled()) {
    mFileSink.stop();
  }

  // Stop output handler
  mOutputHandler.stop();

  // wait for the gui thread
  if (mBuildHistograms && mGuiThread.joinable()) {
    mGuiThread.join();
  }

  LOG(INFO) << "PostRun done... ";
}

void StfSenderDevice::StfReceiverThread()
{
  auto& lInputChan = GetChannel(mInputChannelName, 0);

  InterleavedHdrDataDeserializer lStfReceiver;
  std::unique_ptr<SubTimeFrame> lStf;

  // wait for the device to go into RUNNING state
  WaitForRunningState();

  while ((lStf = lStfReceiver.deserialize(lInputChan)) != nullptr) {

    const TimeFrameIdType lStfId = lStf->header().mId;

    { // rate-limited LOG: print stats every 100 TFs
      static unsigned long floodgate = 0;
      if (++floodgate % 100 == 1)
        LOG(DEBUG) << "TF[" << lStfId << "] size: " << lStf->getDataSize();
    }

    queue(eReceiverOut, std::move(lStf));
  }

  LOG(INFO) << "Exiting StfOutputThread...";
}

void StfSenderDevice::GuiThread()
{
  std::unique_ptr<TH1S> lStfPipelinedCntHist = std::make_unique<TH1S>("StfQueuedH", "Queued STFs", 150, -0.5, 150 - 0.5);
  lStfPipelinedCntHist->GetXaxis()->SetTitle("Number of queued Stf");

  // wait for the device to go into RUNNING state
  WaitForRunningState();

  while (CheckCurrentState(RUNNING)) {
    LOG(INFO) << "Updating histograms...";

    mGui->Canvas().cd(1);
    mGui->DrawHist(lStfPipelinedCntHist.get(), this->getPipelinedSizeSamples());

    mGui->Canvas().Modified();
    mGui->Canvas().Update();

    std::this_thread::sleep_for(5s);
  }
  LOG(INFO) << "Exiting GUI thread...";
}

bool StfSenderDevice::ConditionalRun()
{
  // nothing to do here sleep for awhile
  std::this_thread::sleep_for(200ms);
  return true;
}
}
} /* namespace o2::DataDistribution */
