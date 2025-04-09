/*
 * Copyright (c) 2017 Jason Lowe-Power
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "learning_gem5/part2/simple_cache.hh"

#include "base/compiler.hh"
#include "base/random.hh"
#include "debug/SimpleCache.hh"
#include "sim/system.hh"

namespace gem5
{

SimpleCache::SimpleCache(const SimpleCacheParams &params) :
    ClockedObject(params),
    latency(params.latency),
    blockSize(params.system->cacheLineSize()),
    capacity(params.size / blockSize),
    cpuPort(params.name + ".cpu_side", this),
    memPort(params.name + ".mem_side", this),
    blocked_membus(false),blocked_memctrl(false),stats(this)
{

}

Port &
SimpleCache::getPort(const std::string &if_name, PortID idx)
{
    // This is the name from the Python SimObject declaration in SimpleCache.py
    if (if_name == "mem_side") {
        panic_if(idx != InvalidPortID,
                 "Mem side of simple cache not a vector port");
        return memPort;
    } else if (if_name == "cpu_side" ) {
        // We should have already created all of the ports in the constructor
        return cpuPort;
    } else {
        // pass it along to our super class
        return ClockedObject::getPort(if_name, idx);
    }
}

bool 
SimpleCache::CPUSidePort::sendPacket(PacketPtr pkt)
{
    // Note: This flow control is very simple since the cache is blocking.

    panic_if(blocked, "Should never try to send if blocked!");

    // If we can't send the packet across the port, store it for later.
    DPRINTF(SimpleCache, "Sending %s to CPU\n", pkt->print());
    if (!sendTimingResp(pkt)) {
        DPRINTF(SimpleCache, "failed!\n");
        // blockedPacket = pkt;
        blocked  = true;
        return false;
    }else {
        trySendRetry();
        return true;
    }
}

// void
// SimpleCache::CPUSidePort::trySendRetry()
// {
//     if (needRetry && !blocked ) {
//         needRetry = false;
//         DPRINTF(CPUSidePort, "bus_side,Sending retry req for %d\n", id);
//         sendRetryReq();
//     }
// }

AddrRangeList
SimpleCache::CPUSidePort::getAddrRanges() const
{
    DPRINTF(SimpleCache, "getAddrRanges\n");
    return owner->getAddrRanges();
}

void
SimpleCache::CPUSidePort::trySendRetry()
{
    DPRINTF(SimpleCache, "CPUSidePort trySendRetry. needRetry %d  blocked %d blocked_memctrl %d\n",needRetry,blocked ,owner->blocked_memctrl);
    if (needRetry && !blocked) {
        // Only send a retry if the port is now completely free
        needRetry = false;
        owner->blocked_memctrl = false;
        DPRINTF(SimpleCache, "Sending retry req.\n");
        sendRetryReq();
    }
}

void
SimpleCache::CPUSidePort::recvFunctional(PacketPtr pkt)
{
    DPRINTF(SimpleCache, "recvFunctional %s to CPU\n", pkt->print());
    // Just forward to the cache.
    owner->handleFunctional(pkt);
}

bool
SimpleCache::CPUSidePort::recvTimingReq(PacketPtr pkt)
{
    DPRINTF(SimpleCache, "Got request %s\n", pkt->print());
    
    if (blocked||needRetry) {
        // The cache may not be able to send a reply if this is blocked
        DPRINTF(SimpleCache, "Request blocked\n");
        needRetry = true;
        // owner->memPort.sendPacket(pkt);
            // port.trySendRetry();
        return false;
    }
    // Just forward to the cache.
    if (!owner->handleRequest(pkt)) {
        DPRINTF(SimpleCache, "Request failed\n");
        // stalling
        needRetry = true;
        return false;
    } else {
        DPRINTF(SimpleCache, "Request succeeded\n");
        return true;
    }
}

void
SimpleCache::CPUSidePort::recvRespRetry()
{
    // We should have a blocked packet if this function is called.
    assert(blocked);

    // Grab the blocked packet.
    // PacketPtr pkt = blockedPacket;
    // blockedPacket = nullptr;
    blocked = false;
    DPRINTF(SimpleCache, "recvRespRetry \n");
    // Try to resend it. It's possible that it fails again.
    // sendPacket(pkt);
    owner->blocked_membus = false;
    // We may now be able to accept new packets
    owner->memPort.trySendRetry();
}

bool
SimpleCache::MemSidePort::sendPacket(PacketPtr pkt)
{
    // Note: This flow control is very simple since the cache is blocking.

    panic_if(blocked, "Should never try to send if blocked!");
    DPRINTF(SimpleCache, "sendPacket pkt %s\n", pkt->print());
    // If we can't send the packet across the port, store it for later.
    if (!sendTimingReq(pkt)) {
        // blockedPacket = pkt;
        DPRINTF(SimpleCache, "MemSidePort sendPacket pkt %s fail\n", pkt->print());
        blocked = true;
        return false;
    }else{
        DPRINTF(SimpleCache, "MemSidePort sendPacket success \n");
        owner->cpuPort.trySendRetry();
        return true;
    }
}

bool
SimpleCache::MemSidePort::recvTimingResp(PacketPtr pkt)
{
    // Just forward to the cache.
    DPRINTF(SimpleCache, "recvTimingResp pkt %s\n", pkt->print());
    if(!owner->handleResponse(pkt)){
        needRetry = true;
        return false;
    }else{
        return true;
    }
}

void
SimpleCache::MemSidePort::recvReqRetry()
{
    // We should have a blocked packet if this function is called.
    assert(blocked);

    DPRINTF(SimpleCache, "recvReqRetry\n");
    // Grab the blocked packet.
    
    blocked = false;
    // for (auto& port : owner->cpuPort) {
         
    // }
    owner->cpuPort.trySendRetry();
}

void
SimpleCache::MemSidePort::recvRangeChange()
{
    DPRINTF(SimpleCache, "recvRangeChange \n");
    owner->sendRangeChange();
}

void
SimpleCache::MemSidePort::trySendRetry()
{
    if (needRetry && !blocked) {
        // Only send a retry if the port is now completely free
        needRetry = false;
        DPRINTF(SimpleCache, "Sending retry req.\n");
        sendRetryResp();
    }
}

bool
SimpleCache::handleRequest(PacketPtr pkt)
{
    if (blocked_memctrl) {
        // There is currently an outstanding request so we can't respond. Stall
        DPRINTF(SimpleCache, "Got request for addr %#x ,but handleRequest blocked \n", pkt->getAddr());
        return false;
    }

    DPRINTF(SimpleCache, "Got request for addr %#x\n", pkt->getAddr());

    // This cache is now blocked waiting for the response to this packet.
    // blocked = true;
    blocked_memctrl = true;
    // Store the port for when we get the response
    // assert(waitingPortId == -1);
    // waitingPortId = port_id;
    // accessTiming(pkt);
    // Schedule an event after cache access latency to actually access
    // if(originalPacket == nullptr){
        schedule(new EventFunctionWrapper([this, pkt]{ accessTiming(pkt); },
                                    name() + ".accessEvent", true),
            clockEdge(latency));
    // }
    // memPort.sendPacket(pkt);
    // if(!memPort.sendPacket(pkt)){
    //     blocked_memctrl = true;
    //     return false;
    // }
    return true;
}

bool
SimpleCache::handleResponse(PacketPtr pkt)
{
    // assert(blocked);
    DPRINTF(SimpleCache, "Got response for addr %#x\n", pkt->getAddr());
    if(blocked_membus){
         DPRINTF(SimpleCache, "but blocked_membus true");
        return  false;
    }
    // For now assume that inserts are off of the critical path and don't count
    // for any added latency.
    // insert(pkt);

    stats.missLatency.sample(curTick() - missTime);

    // If we had to upgrade the request packet to a full cache line, now we
    // // can use that packet to construct the response.
    // if (originalPacket != nullptr) {
    //     DPRINTF(SimpleCache, "Copying data from new packet to old\n");
    //     // We had to upgrade a previous packet. We can functionally deal with
    //     // the cache access now. It better be a hit.
    //     [[maybe_unused]] bool hit = accessFunctional(originalPacket);
    //     panic_if(!hit, "Should always hit after inserting");
    //     originalPacket->makeResponse();
    //     delete pkt; // We may need to delay this, I'm not sure.
    //     pkt = originalPacket;
    //     originalPacket = nullptr;
    // } // else, pkt contains the data it needs
    // int port = waitingPortId;

    // The packet is now done. We're about to put it in the port, no need for
    // this object to continue to stall.
    // We need to free the resource before sending the packet in case the CPU
    // tries to send another request immediately (e.g., in the same callchain).
    // blocked = false;
    // waitingPortId = -1;

    // Simply forward to the memory port
    if(!cpuPort.sendPacket(pkt)){
        blocked_membus = true;
        return false;
    }

    // For each of the cpu ports, if it needs to send a retry, it should do it
    // now since this memory object may be unblocked now.
    // for (auto& port : cpuPort) {
    //     port.trySendRetry();
    // }
    // sendResponse(pkt);

    return true;
}

// void SimpleCache::sendResponse(PacketPtr pkt)
// {
//     // assert(blocked);
//     DPRINTF(SimpleCache, "Sending resp for addr %#x\n", pkt->getAddr());

//     // int port = waitingPortId;

//     // The packet is now done. We're about to put it in the port, no need for
//     // this object to continue to stall.
//     // We need to free the resource before sending the packet in case the CPU
//     // tries to send another request immediately (e.g., in the same callchain).
//     // blocked = false;
//     // waitingPortId = -1;

//     // Simply forward to the memory port
//     cpuPort.sendPacket(pkt);

//     // For each of the cpu ports, if it needs to send a retry, it should do it
//     // now since this memory object may be unblocked now.
//     for (auto& port : cpuPort) {
//         port.trySendRetry();
//     }
// }

void
SimpleCache::handleFunctional(PacketPtr pkt)
{
    DPRINTF(SimpleCache, "handleFunctional %#x\n", pkt->getAddr());
    // if (accessFunctional(pkt)) {
    //     pkt->makeResponse();
    // } else {
        memPort.sendFunctional(pkt);
    // }
}

void
SimpleCache::accessTiming(PacketPtr pkt)
{
    // bool hit = accessFunctional(pkt);

    // DPRINTF(SimpleCache, "%s for packet: %s\n", hit ? "Hit" : "Miss",
    //         pkt->print());

    // if (hit) {
    //     // Respond to the CPU side
    //     stats.hits++; // update stats
    //     DDUMP(SimpleCache, pkt->getConstPtr<uint8_t>(), pkt->getSize());
    //     pkt->makeResponse();
    //     sendResponse(pkt);
    // } else {
        stats.misses++; // update stats
        missTime = curTick();
        // Forward to the memory side.
        // We can't directly forward the packet unless it is exactly the size
        // of the cache line, and aligned. Check for that here.
        Addr addr = pkt->getAddr();
        Addr block_addr = pkt->getBlockAddr(blockSize);
        unsigned size = pkt->getSize();
        Tick test = latency;
        // if (addr == block_addr && size == blockSize) {
        //     // Aligned and block size. We can just forward.
            DPRINTF(SimpleCache, "forwarding packet blocked_membus %d blocked_memctrl %d latency %d \n",blocked_membus,blocked_memctrl,test);
        if(!memPort.sendPacket(pkt)){
            DPRINTF(SimpleCache, "Failed to send packet after delay\n");
            blocked_memctrl = true;
        } else {
            blocked_memctrl = false;
        }
        // } //else {
        //     DPRINTF(SimpleCache, "Upgrading packet to block size\n");
        //     panic_if(addr - block_addr + size > blockSize,
        //              "Cannot handle accesses that span multiple cache lines");
        //     // Unaligned access to one cache block
        //     assert(pkt->needsResponse());
        //     MemCmd cmd;
        //     if (pkt->isWrite() || pkt->isRead()) {
        //         // Read the data from memory to write into the block.
        //         // We'll write the data in the cache (i.e., a writeback cache)
        //         cmd = MemCmd::ReadReq;
        //     } else {
        //         panic("Unknown packet type in upgrade size");
        //     }

        //     // Create a new packet that is blockSize
        //     PacketPtr new_pkt = new Packet(pkt->req, cmd, blockSize);
        //     new_pkt->allocate();

        //     // Should now be block aligned
        //     assert(new_pkt->getAddr() == new_pkt->getBlockAddr(blockSize));

        //     // Save the old packet
        //     originalPacket = pkt;

        //     DPRINTF(SimpleCache, "forwarding packet\n");
        //     memPort.sendPacket(new_pkt);
        // }
    // }
}

bool
SimpleCache::accessFunctional(PacketPtr pkt)
{
    DPRINTF(SimpleCache, "accessFunctional for addr %#x\n", pkt->getAddr());
    Addr block_addr = pkt->getBlockAddr(blockSize);
    auto it = cacheStore.find(block_addr);
    if (it != cacheStore.end()) {
        if (pkt->isWrite()) {
            // Write the data into the block in the cache
            pkt->writeDataToBlock(it->second, blockSize);
        } else if (pkt->isRead()) {
            // Read the data out of the cache block into the packet
            pkt->setDataFromBlock(it->second, blockSize);
        } else {
            panic("Unknown packet type!");
        }
        return true;
    }
    return false;
}

void
SimpleCache::insert(PacketPtr pkt)
{
    // The packet should be aligned.
    assert(pkt->getAddr() ==  pkt->getBlockAddr(blockSize));
    // The address should not be in the cache
    assert(cacheStore.find(pkt->getAddr()) == cacheStore.end());
    // The pkt should be a response
    assert(pkt->isResponse());

    if (cacheStore.size() >= capacity) {
        // Select random thing to evict. This is a little convoluted since we
        // are using a std::unordered_map. See http://bit.ly/2hrnLP2
        int bucket, bucket_size;
        do {
            bucket = random_mt.random(0, (int)cacheStore.bucket_count() - 1);
        } while ( (bucket_size = cacheStore.bucket_size(bucket)) == 0 );
        auto block = std::next(cacheStore.begin(bucket),
                               random_mt.random(0, bucket_size - 1));

        DPRINTF(SimpleCache, "Removing addr %#x\n", block->first);

        // Write back the data.
        // Create a new request-packet pair
        RequestPtr req = std::make_shared<Request>(
            block->first, blockSize, 0, 0);

        PacketPtr new_pkt = new Packet(req, MemCmd::WritebackDirty, blockSize);
        new_pkt->dataDynamic(block->second); // This will be deleted later

        DPRINTF(SimpleCache, "Writing packet back %s\n", pkt->print());
        // Send the write to memory
        memPort.sendPacket(new_pkt);

        // Delete this entry
        cacheStore.erase(block->first);
    }

    DPRINTF(SimpleCache, "Inserting %s\n", pkt->print());
    DDUMP(SimpleCache, pkt->getConstPtr<uint8_t>(), blockSize);

    // Allocate space for the cache block data
    uint8_t *data = new uint8_t[blockSize];

    // Insert the data and address into the cache store
    cacheStore[pkt->getAddr()] = data;

    // Write the data into the cache
    pkt->writeDataToBlock(data, blockSize);
}

AddrRangeList
SimpleCache::getAddrRanges() const
{
    DPRINTF(SimpleCache, "Sending new ranges\n");
    // Just use the same ranges as whatever is on the memory side.
    return memPort.getAddrRanges();
}

void
SimpleCache::sendRangeChange() const
{
     DPRINTF(SimpleCache, "sendRangeChange \n");
    // for (auto& port : cpuPort) {
        cpuPort.sendRangeChange();
    // }
}

SimpleCache::SimpleCacheStats::SimpleCacheStats(statistics::Group *parent)
      : statistics::Group(parent),
      ADD_STAT(hits, statistics::units::Count::get(), "Number of hits"),
      ADD_STAT(misses, statistics::units::Count::get(), "Number of misses"),
      ADD_STAT(missLatency, statistics::units::Tick::get(),
               "Ticks for misses to the cache"),
      ADD_STAT(hitRatio, statistics::units::Ratio::get(),
               "The ratio of hits to the total accesses to the cache",
               hits / (hits + misses))
{
    missLatency.init(16); // number of buckets
}

} // namespace gem5
