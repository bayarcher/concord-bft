// Concord
//
// Copyright (c) 2021 VMware, Inc. All Rights Reserved.
//
// This product is licensed to you under the Apache 2.0 license (the "License").  You may not use this product except in
// compliance with the Apache 2.0 License.
//
// This product may include a number of subcomponents with separate copyright notices and license terms. Your use of
// these subcomponents is subject to the terms and conditions of the subcomponent's license, as noted in the LICENSE
// file.
#include <cstring>
#include "OpenTracing.hpp"
#include "ReplicasRestartReadyProofMsg.hpp"
#include "SysConsts.hpp"
#include "Crypto.hpp"
#include "SigManager.hpp"

namespace bftEngine {
namespace impl {

ReplicasRestartReadyProofMsg::ReplicasRestartReadyProofMsg(ReplicaId senderId,
                                                           SeqNum seqNum,
                                                           const concordUtils::SpanContext& spanContext)
    : MessageBase(senderId,
                  MsgCode::ReplicasRestartReadyProof,
                  spanContext.data().size(),
                  ReplicaConfig::instance().getmaxExternalMessageSize() - spanContext.data().size()) {
  b()->genReplicaId = senderId;
  b()->seqNum = seqNum;
  b()->elementsCount = 0;
  b()->locationAfterLast = 0;
  std::memcpy(body() + sizeof(Header), spanContext.data().data(), spanContext.data().size());
}
const uint32_t ReplicasRestartReadyProofMsg::getBodySize() const {
  uint32_t bodySize = b()->locationAfterLast;
  if (bodySize == 0) bodySize = sizeof(Header) + spanContextSize();
  return bodySize;
}

ReplicasRestartReadyProofMsg* ReplicasRestartReadyProofMsg::create(ReplicaId id,
                                                                   SeqNum s,
                                                                   const concordUtils::SpanContext& spanContext) {
  ReplicasRestartReadyProofMsg* m = new ReplicasRestartReadyProofMsg(id, s, spanContext);
  return m;
}

void ReplicasRestartReadyProofMsg::addElement(std::unique_ptr<ReplicaRestartReadyMsg>& restartMsg) {
  if (b()->locationAfterLast == 0)  // if this is the first element
  {
    ConcordAssert(b()->elementsCount == 0);
    b()->locationAfterLast = sizeof(Header) + spanContextSize();
  }
  uint32_t requiredSpace = b()->locationAfterLast + restartMsg->size();
  ConcordAssertLE((size_t)(requiredSpace + SigManager::instance()->getMySigLength()), (size_t)internalStorageSize());
  std::memcpy(body() + b()->locationAfterLast, restartMsg->body(), restartMsg->size());
  b()->elementsCount += 1;
  b()->locationAfterLast = requiredSpace;
}
// +---------------------------------------------------------------+--------------+
// | Msg header(genReplicaId, seqNum, eleCount, locationAfterLast) | Span Context |
// +---------------------------------------------------------------+--------------+
// |  Element1(replicaId, seqNum, sigLen, sigBody)  |
// +------------------------------------------------+
// |     .................................          |
// +------------------------------------------------+
// |  Elementn(replicaId, seqNum, sigLen, sigBody)  |
// +------------------------------------------------+

void ReplicasRestartReadyProofMsg::finalizeMessage() {
  auto bodySize = getBodySize();
  setMsgSize(bodySize);
  shrinkToFit();
}

void ReplicasRestartReadyProofMsg::validate(const ReplicasInfo& repInfo) const {
  auto sigManager = SigManager::instance();
  if (size() < sizeof(Header) + spanContextSize() || !repInfo.isIdOfReplica(idOfGeneratedReplica()))
    throw std::runtime_error(__PRETTY_FUNCTION__ + std::string(": basic validations"));

  auto dataLength = getBodySize();
  uint16_t sigLen = sigManager->getSigLength(idOfGeneratedReplica());

  if (size() < dataLength) throw std::runtime_error(__PRETTY_FUNCTION__ + std::string(": size"));
  if (!checkElements(repInfo, sigLen))  // check elements in message
    throw std::runtime_error(__PRETTY_FUNCTION__ + std::string(": check elements in message"));
}

bool ReplicasRestartReadyProofMsg::checkElements(const ReplicasInfo& repInfo, uint16_t sigSize) const {
  auto sigManager = SigManager::instance();
  uint16_t numOfActualElements = 0;
  uint32_t remainingBytes = size() - sizeof(Header) - spanContextSize();
  char* currLoc = body() + sizeof(Header) + spanContextSize();
  SeqNum seqNum = b()->seqNum;
  while ((remainingBytes >= (sizeof(ReplicaRestartReadyMsg::Header) + sigSize)) &&
         (numOfActualElements < elementsCount())) {
    numOfActualElements++;
    ReplicaRestartReadyMsg::Header* hdr = (ReplicaRestartReadyMsg::Header*)currLoc;
    if (seqNum != hdr->seqNum) return false;
    if (repInfo.myId() != hdr->genReplicaId) {
      if (!sigManager->verifySig(hdr->genReplicaId,
                                 currLoc,
                                 sizeof(ReplicaRestartReadyMsg::Header),
                                 currLoc + sizeof(ReplicaRestartReadyMsg::Header),
                                 hdr->sigLength)) {
        return false;
      }
    }
    const uint32_t s = sizeof(ReplicaRestartReadyMsg::Header) + hdr->sigLength;
    if (remainingBytes < s) return false;
    remainingBytes -= s;
    currLoc += s;
  }
  if (numOfActualElements != elementsCount()) return false;
  if (numOfActualElements > 0) {
    const uint32_t locationAfterLastElement = size() - remainingBytes;
    if (this->b()->locationAfterLast != locationAfterLastElement) return false;
  } else {
    if (this->b()->locationAfterLast != 0) return false;
  }
  return true;
}

}  // namespace impl
}  // namespace bftEngine
