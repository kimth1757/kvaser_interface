/*
* Unpublished Copyright (c) 2009-2019 AutonomouStuff, LLC, All Rights Reserved.
*
* This file is part of the Kvaser ROS 1.0 driver which is released under the MIT license.
* See file LICENSE included with this software or go to https://opensource.org/licenses/MIT for full license details.
*/

#include <kvaser_interface/kvaser_interface.h>

#include <string>
#include <vector>

using namespace AS::CAN;

KvaserCan::KvaserCan() :
  handle(new int32_t)
{
  *handle = -1;
  canInitializeLibrary();
}

KvaserCan::~KvaserCan()
{
  if (*handle > -1)
    canClose(*handle);
}

ReturnStatuses KvaserCan::open(const int32_t& hardware_id,
                               const int32_t& circuit_id,
                               const int32_t& bitrate,
                               const bool& echo_on)
{
  if (!on_bus)
  {
    int32_t numChan;
    ReturnStatuses stat;

    stat = KvaserCanUtils::getChannelCount(&numChan);

    if (stat != ReturnStatuses::OK)
      return stat;

    uint32_t serial[2];
    uint32_t channel_number;
    int32_t channel = -1;

    for (int32_t idx = 0; idx < numChan; idx++)
    {
      if (canGetChannelData(idx, canCHANNELDATA_CARD_SERIAL_NO, &serial, sizeof(serial)) == canOK)
      {
        if (serial[0] == (uint32_t) hardware_id)
        {
          if (canGetChannelData(idx, canCHANNELDATA_CHAN_NO_ON_CARD, &channel_number, sizeof(channel_number)) == canOK)
          {
            if (channel_number == (uint32_t) circuit_id)
            {
              channel = idx;
            }
          }
        }
      }
    }

    if (channel == -1)
    {
      return ReturnStatuses::BAD_PARAM;
    }

    // Open channel
    *handle = canOpenChannel(channel, canOPEN_ACCEPT_VIRTUAL);

    if (*handle < 0)
      return ReturnStatuses::INIT_FAILED;

    // Set bit rate and other parameters
    int64_t freq;

    switch (bitrate)
    {
      case 125000: freq = canBITRATE_125K; break;
      case 250000: freq = canBITRATE_250K; break;
      case 500000: freq = canBITRATE_500K; break;
      case 1000000: freq = canBITRATE_1M; break;
      default: return  ReturnStatuses::BAD_PARAM;
    }

    if (canSetBusParams(*handle, freq, 0, 0, 0, 0, 0) < 0)
      return ReturnStatuses::BAD_PARAM;

    // Linuxcan defaults to echo on, so if you've opened the same can channel
    // from multiple interfaces they will receive the messages that each other
    // send.  Turn it off here if desired.
    if (!echo_on)
    {
      uint8_t off = 0;
      canIoCtl(*handle, canIOCTL_SET_LOCAL_TXECHO, &off, 1);
    }

    // Set output control
    canSetBusOutputControl(*handle, canDRIVER_NORMAL);

    if (canBusOn(*handle) < 0)
      return ReturnStatuses::INIT_FAILED;

    on_bus = true;
  }

  return ReturnStatuses::OK;
}

bool KvaserCan::isOpen()
{
  if (*handle < 0)
  {
    return false;
  }
  else
  {
    if (on_bus)
    {
      uint64_t flags;

      canStatus ret = canReadStatus(*handle, &flags);

      if (ret != canOK)
        return false;

      if ((flags & canSTAT_BUS_OFF) > 1)
      {
        close();
        return false;
      }
      else
      {
        return true;
      }
    }
    else
    {
      return false;
    }
  }
}

ReturnStatuses KvaserCan::close()
{
  if (*handle < 0)
    return ReturnStatuses::CHANNEL_CLOSED;

  // Close the channel
  if (canClose(*handle) != canOK)
    return ReturnStatuses::CLOSE_FAILED;

  *handle = -1;
  on_bus = false;

  return ReturnStatuses::OK;
}

ReturnStatuses KvaserCan::read(int64_t *id,
                               uint8_t *msg,
                               uint32_t *size,
                               bool *extended,
                               uint64_t *time)
{
  if (*handle < 0)
  {
    return ReturnStatuses::CHANNEL_CLOSED;
  }

  bool done = false;
  ReturnStatuses ret_val = ReturnStatuses::INIT_FAILED;
  unsigned int flag = 0;

  while (!done)
  {
    canStatus ret = canRead(*handle, id, msg, size, &flag, time);

    if (ret == canERR_NOTINITIALIZED)
    {
      ret_val = ReturnStatuses::CHANNEL_CLOSED;
      on_bus = false;
      done = true;
    }
    else if (ret == canERR_NOMSG)
    {
      ret_val = ReturnStatuses::NO_MESSAGES_RECEIVED;
      done = true;
    }
    else if (ret != canOK)
    {
      ret_val = ReturnStatuses::READ_FAILED;
      done = true;
    }
    else if (!(flag & 0xF9))
    {
      // Was a received message with actual data
      ret_val = ReturnStatuses::OK;
      done = true;
    }
    // Else a protocol message, such as a TX ACK, was received
    // Keep looping until one of the other conditions above is met
  }

  if (ret_val == ReturnStatuses::OK)
    *extended = ((flag & canMSG_EXT) > 0);

  return ret_val;
}

ReturnStatuses KvaserCan::write(const int64_t& id,
                                uint8_t *msg,
                                const uint32_t& size,
                                const bool& extended)
{
  if (*handle < 0)
    return ReturnStatuses::CHANNEL_CLOSED;

  uint32_t flag;

  if (extended)
    flag = canMSG_EXT;
  else
    flag = canMSG_STD;

  canStatus ret = canWrite(*handle, id, msg, size, flag);

  return (ret == canOK) ? ReturnStatuses::OK : ReturnStatuses::WRITE_FAILED;
}

ReturnStatuses KvaserCanUtils::canlibStatToReturnStatus(const int32_t & canlibStat)
{
  switch (canlibStat)
  {
    case canOK:
      return ReturnStatuses::OK;
    case canERR_PARAM:
      return ReturnStatuses::BAD_PARAM;
    case canERR_NOTFOUND:
      return ReturnStatuses::NO_CHANNELS_FOUND;
    default:
      return ReturnStatuses::INIT_FAILED;
  }
}

ReturnStatuses KvaserCanUtils::getChannelCount(int32_t * numChan)
{
  auto stat = canGetNumberOfChannels(numChan);

  return canlibStatToReturnStatus(stat);
}

std::vector<KvaserChannel> KvaserCanUtils::getChannels()
{
  std::vector<KvaserChannel> channels;

  int32_t numChan = -1;
  ReturnStatuses retStat = ReturnStatuses::OK;

  retStat = KvaserCanUtils::getChannelCount(&numChan);

  // Sanity checks before continuing
  if (retStat == ReturnStatuses::OK &&
    numChan > -1 &&
    numChan < 300)
  {
    for (auto i = 0; i < numChan; ++i)
    {
      KvaserChannel chan;
      int stat = 0;

      chan.channel_no = i;

      uint64_t serial = 0;
      uint32_t card_no = 0;
      uint32_t channel_no = 0;
      uint32_t card_type = 0;
      uint16_t firmware_rev[4];
      uint32_t max_bitrate = 0;

      stat = canGetChannelData(i, canCHANNELDATA_CARD_SERIAL_NO, &serial, sizeof(serial));

      if (stat == canOK)
        chan.serial_no = serial;
      else
        chan.all_data_valid = false;

      stat = canGetChannelData(i, canCHANNELDATA_CARD_NUMBER, &card_no, sizeof(card_no));

      if (stat == canOK)
        chan.card_no = card_no;
      else
        chan.all_data_valid = false;

      stat = canGetChannelData(i, canCHANNELDATA_CHAN_NO_ON_CARD, &channel_no, sizeof(channel_no));

      if (stat == canOK)
        chan.channel_no_on_card = channel_no;
      else
        chan.all_data_valid = false;

      stat = canGetChannelData(i, canCHANNELDATA_CARD_TYPE, &card_type, sizeof(card_type));

      if (stat == canOK)
        chan.hw_type = static_cast<HardwareType>(card_type);
      else
        chan.all_data_valid = false;

      stat = canGetChannelData(i, canCHANNELDATA_CARD_FIRMWARE_REV, &firmware_rev, sizeof(firmware_rev));

      if (stat == canOK)
      {
        chan.firmware_rev_maj = firmware_rev[0];
        chan.firmware_rev_min = firmware_rev[1];
        chan.firmware_rev_rel = firmware_rev[2];
        chan.firmware_rev_bld = firmware_rev[3];
      }
      else
      {
        chan.all_data_valid = false;
      }

      stat = canGetChannelData(i, canCHANNELDATA_MAX_BITRATE, &max_bitrate, sizeof(max_bitrate));

      if (stat == canOK)
        chan.max_bitrate = max_bitrate;
      else
        chan.all_data_valid = false;

      channels.push_back(chan);
    }
  }

  return channels;
}

std::string KvaserCanUtils::returnStatusDesc(const ReturnStatuses& ret)
{
  std::string status_string;

  if (ret == ReturnStatuses::INIT_FAILED)
    status_string = "Initialization of the CAN interface failed.";
  else if (ret == ReturnStatuses::BAD_PARAM)
    status_string = "A bad parameter was provided to the CAN interface during initalization.";
  else if (ret == ReturnStatuses::NO_CHANNELS_FOUND)
    status_string = "No available CAN channels were found.";
  else if (ret == ReturnStatuses::CHANNEL_CLOSED)
    status_string = "CAN channel is not currently open.";
  else if (ret == ReturnStatuses::NO_MESSAGES_RECEIVED)
    status_string = "No messages were received on the interface.";
  else if (ret == ReturnStatuses::READ_FAILED)
    status_string = "A read operation failed on the CAN interface.";
  else if (ret == ReturnStatuses::WRITE_FAILED)
    status_string = "A write operation failed on the CAN interface.";
  else if (ret == ReturnStatuses::CLOSE_FAILED)
    status_string = "Closing the CAN interface failed.";

  return status_string;
}
