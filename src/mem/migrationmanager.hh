/**
 * @file
 * migrationmanager.hh
 * Last modify Time:2022.3.16
 * Author:hjy
 */
#ifndef __MigrationManager_HH__
#define __MigrationManager_HH__

#include "mem/port.hh"
#include "params/MigrationManager.hh"
#include "sim/sim_object.hh"
#include "sim/stats.hh"
#include "sim/system.hh"
#include <queue>

class System;
class AccessCounter;

class MigrationManager : public SimObject {
    private:
        // class define : port 1, 接收AC发送的包，触发dram->nvm的迁移
        class AcSidePort : public ResponsePort {   // receive the migration request from Access-counter
            private:
                MigrationManager& migrationmanager;
                bool blocked;
                bool needRetry;     // if need retry
                
            public:
                AcSidePort(const std::string& _name, MigrationManager& _migrationmanager);   // constructor
                AddrRangeList getAddrRanges() const override;
                void sendPacket(PacketPtr pkt);
                void trySendRetry();

            protected:
                Tick recvAtomic(PacketPtr pkt) override;
                void recvFunctional(PacketPtr pkt) override;
                bool recvTimingReq(PacketPtr pkt) override;
                void recvRespRetry() override; 
        };
        // port 2, 接收RT发送的包，触发nvm dram hbm三者之间可能的迁移
        class RtResponsePort : public ResponsePort {   // receive the migration request from Access-counter
            private:
                MigrationManager& migrationmanager;
                bool blocked;
                bool needRetry;     // if need retry
                
            public:
                RtResponsePort(const std::string& _name, MigrationManager& _migrationmanager);   // constructor
                AddrRangeList getAddrRanges() const override;
                void sendPacket(PacketPtr pkt);
                void trySendRetry();

            protected:
                Tick recvAtomic(PacketPtr pkt) override;
                void recvFunctional(PacketPtr pkt) override;
                bool recvTimingReq(PacketPtr pkt) override;
                void recvRespRetry() override; 
        };

        // Prot 3,向RT发送Request包，告知其某一次迁移已完成
        class RtRequestPort : public RequestPort {   // send message to remapping table
            private:
                MigrationManager& migrationmanager;
                bool blocked;
                int counter;        // record the num of packets we have send to rt;
                bool needRetry;     // if need retry
            public:
                RtRequestPort(const std::string& _name, MigrationManager& _migrationmanager);   // constructor
                bool sendPacket(PacketPtr pkt);
                void trySendRetry();
                
            protected:
                bool recvTimingResp(PacketPtr pkt) override;
                void recvReqRetry() override;
                void recvRangeChange() override;
        };

        //port 4, 向DP发送迁移需要的读写包
        class DispatcherSidePort : public RequestPort {     // send migratio request to dispatcher
              private:
                MigrationManager& migrationmanager;
                bool blocked;
                bool needRetry;     // if need retry
                PacketPtr blockedPacket;


            public:
                DispatcherSidePort(const std::string& _name, MigrationManager& _migrationmanager);
                // constructor
                bool sendPacket(PacketPtr pkt);
                void trySendRetry();

            protected:
                bool recvTimingResp(PacketPtr pkt) override;
                void recvReqRetry() override;
                void recvRangeChange() override;
        };

        // declartion of the ports
        AcSidePort ac_side_port;
        RtResponsePort rt_response_port;
        RtRequestPort rt_request_port;
        DispatcherSidePort dp_side_port;

        Tick handleAtomic(PacketPtr pkt);
        void handleFunctional(PacketPtr pkt);
        bool IsDispatcher(PacketPtr pkt);           // 判断是哪个发来的请求（disp还是remapping table）
        // deal functions
        bool handleACRequest(PacketPtr pkt);        // deal the request from access counter
        bool handleRTRequest(PacketPtr pkt);
        bool handleRTResponse(PacketPtr pkt);
        bool handleDispResponse(PacketPtr pkt);
        void swapPage();
        
        //不同情况下进行迁移
        bool flatnvmtodram_hot(PacketPtr pkt);
        bool flatnvmtodram_first(PacketPtr pkt); 
        bool flatnvmtodram_second(PacketPtr pkt);
        bool cachenvmtodram_free(PacketPtr pkt);
        bool cachenvmtodram_clean(PacketPtr pkt);
        bool cachenvmtodram_dirty(PacketPtr pkt);
        // extra func
        AddrRangeList getAddrRanges() const;
        void sendRangeChange();

        std::queue<PacketPtr> swap_pkts_qe;          // sawp queue, used to save the migration request pkt;
        std::queue<PacketPtr> send_to_dram;
        PacketDataPtr hbm_buffer, dram_buffer, nvm_buffer;       // save the data that read from hbm, bhm;
        int max_queue_len;         // the max length of the queue
        int PageSize;
        int MMnumofcycles; // 发包循环数(阻塞时保护现场)
        bool blockpacket1;//dram read
        bool blockpacket2;//dram write
        bool blockpacket3;//nvm read
        bool blockpacket4;//nvm write
        bool blockpacket5;//dram_cache read
        bool blockpacket6;//dram_cache write
        bool MMblock;


  public:
        MigrationManager(const MigrationManagerParams &params);
        Port &getPort(const std::string &if_name,
                    PortID idx = InvalidPortID) override;
        // overhead
        System* system() const { return _system; }
        void system(System *sys) { _system = sys; }
        AccessCounter* accesscounter() const { return _ac; }
        void accesscounter(AccessCounter *ac) { _ac = ac; }
        RemappingTable* remappingtable() const {return _rt;}
        void remappingtable(RemappingTable *rt) {_rt = rt;}


  protected:

    System *_system;
    AccessCounter* _ac;
    RemappingTable* _rt;
    
    struct MemStats : public Stats::Group {
      MemStats(MigrationManager &_mm);

      void regStats() override;

      const MigrationManager &mm;

      /** MM Number of packets from ac */
      Stats::Vector MigraFromAc;
      /** MM Number of packets of flat_hot*/
      Stats::Vector Migraflathot;
      /** MM Number of packets of flat_first*/
      Stats::Vector Migraflatfir;
      /** MM Number of packets of flat_second*/
      Stats::Vector Migraflatsec;
      /** MM Number of packets of cache_free*/
      Stats::Vector Migracachefree;
      /** MM Number of packets of cache_clean*/
      Stats::Vector Migracachecl;
      /** MM Number of packets of cache_dirty*/
      Stats::Vector Migracachedir;
      /** MM Number of migration */
      Stats::Vector numofMigra;
      /** Average MM Number of packets from ac per second */
      Stats::Formula avMM;

    } stats;
    // overhead 

};

#endif