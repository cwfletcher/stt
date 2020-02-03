/*
 * Copyright (c) 2012 ARM Limited
 * All rights reserved
 *
 * The license below extends only to copyright in the software and shall
 * not be construed as granting a license to any other intellectual
 * property including but not limited to intellectual property relating
 * to a hardware implementation of the functionality of the software
 * licensed hereunder.  You may use the software subject to the license
 * terms below provided that you ensure that this notice is replicated
 * unmodified and in its entirety in all distributions of the software,
 * modified or unmodified, in source code or in binary form.
 *
 * Copyright (c) 2004-2006 The Regents of The University of Michigan
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
 *
 * Authors: Kevin Lim
 *          Korey Sewell
 */

#ifndef __CPU_O3_ROB_IMPL_HH__
#define __CPU_O3_ROB_IMPL_HH__

#include <list>

#include "cpu/o3/rob.hh"
#include "debug/Fetch.hh"
#include "debug/ROB.hh"
#include "params/DerivO3CPU.hh"

using namespace std;

template <class Impl>
ROB<Impl>::ROB(O3CPU *_cpu, DerivO3CPUParams *params)
    : cpu(_cpu),
      numEntries(params->numROBEntries),
      squashWidth(params->squashWidth),
      numInstsInROB(0),
      numThreads(params->numThreads)
{
    std::string policy = params->smtROBPolicy;

    //Convert string to lowercase
    std::transform(policy.begin(), policy.end(), policy.begin(),
                   (int(*)(int)) tolower);

    //Figure out rob policy
    if (policy == "dynamic") {
        robPolicy = Dynamic;

        //Set Max Entries to Total ROB Capacity
        for (ThreadID tid = 0; tid < numThreads; tid++) {
            maxEntries[tid] = numEntries;
        }

    } else if (policy == "partitioned") {
        robPolicy = Partitioned;
        DPRINTF(Fetch, "ROB sharing policy set to Partitioned\n");

        //@todo:make work if part_amt doesnt divide evenly.
        int part_amt = numEntries / numThreads;

        //Divide ROB up evenly
        for (ThreadID tid = 0; tid < numThreads; tid++) {
            maxEntries[tid] = part_amt;
        }

    } else if (policy == "threshold") {
        robPolicy = Threshold;
        DPRINTF(Fetch, "ROB sharing policy set to Threshold\n");

        int threshold =  params->smtROBThreshold;;

        //Divide up by threshold amount
        for (ThreadID tid = 0; tid < numThreads; tid++) {
            maxEntries[tid] = threshold;
        }
    } else {
        assert(0 && "Invalid ROB Sharing Policy.Options Are:{Dynamic,"
                    "Partitioned, Threshold}");
    }

    resetState();
}

template <class Impl>
void
ROB<Impl>::resetState()
{
    for (ThreadID tid = 0; tid  < numThreads; tid++) {
        doneSquashing[tid] = true;
        threadEntries[tid] = 0;
        squashIt[tid] = instList[tid].end();
        squashedSeqNum[tid] = 0;
    }
    numInstsInROB = 0;

    // Initialize the "universal" ROB head & tail point to invalid
    // pointers
    head = instList[0].end();
    tail = instList[0].end();
}

template <class Impl>
std::string
ROB<Impl>::name() const
{
    return cpu->name() + ".rob";
}

template <class Impl>
void
ROB<Impl>::setActiveThreads(list<ThreadID> *at_ptr)
{
    DPRINTF(ROB, "Setting active threads list pointer.\n");
    activeThreads = at_ptr;
}

template <class Impl>
void
ROB<Impl>::drainSanityCheck() const
{
    for (ThreadID tid = 0; tid  < numThreads; tid++)
        assert(instList[tid].empty());
    assert(isEmpty());
}

template <class Impl>
void
ROB<Impl>::takeOverFrom()
{
    resetState();
}

template <class Impl>
void
ROB<Impl>::resetEntries()
{
    if (robPolicy != Dynamic || numThreads > 1) {
        int active_threads = activeThreads->size();

        list<ThreadID>::iterator threads = activeThreads->begin();
        list<ThreadID>::iterator end = activeThreads->end();

        while (threads != end) {
            ThreadID tid = *threads++;

            if (robPolicy == Partitioned) {
                maxEntries[tid] = numEntries / active_threads;
            } else if (robPolicy == Threshold && active_threads == 1) {
                maxEntries[tid] = numEntries;
            }
        }
    }
}

template <class Impl>
int
ROB<Impl>::entryAmount(ThreadID num_threads)
{
    if (robPolicy == Partitioned) {
        return numEntries / num_threads;
    } else {
        return 0;
    }
}

template <class Impl>
int
ROB<Impl>::countInsts()
{
    int total = 0;

    for (ThreadID tid = 0; tid < numThreads; tid++)
        total += countInsts(tid);

    return total;
}

template <class Impl>
int
ROB<Impl>::countInsts(ThreadID tid)
{
    return instList[tid].size();
}

template <class Impl>
void
ROB<Impl>::insertInst(DynInstPtr &inst)
{
    assert(inst);

    robWrites++;

    DPRINTF(ROB, "Adding inst PC %s to the ROB.\n", inst->pcState());

    assert(numInstsInROB != numEntries);

    ThreadID tid = inst->threadNumber;

    /*** [Jiyong,STT] add logic for setting argProducers ***/
    for (auto prevInstIt = instList[tid].begin(); prevInstIt != instList[tid].end(); prevInstIt++){
        // find matched physical reg between prev instr and inst
        DynInstPtr prevInst = (*prevInstIt);
        for (int i = 0; i < inst->numSrcRegs(); i++) {
            if (inst->srcRegIdx(i).index() == 16)   // exclude zero register (zero register cannot be tainted)
                continue;
            for (int j = 0; j < prevInst->numDestRegs(); j++){
                if (inst->renamedSrcRegIdx(i) == prevInst->renamedDestRegIdx(j)){
                    inst->setArgProducer(i, prevInst);
                }
            }
        }
    }

    instList[tid].push_back(inst);

    //Set Up head iterator if this is the 1st instruction in the ROB
    if (numInstsInROB == 0) {
        head = instList[tid].begin();
        assert((*head) == inst);
    }

    //Must Decrement for iterator to actually be valid  since __.end()
    //actually points to 1 after the last inst
    tail = instList[tid].end();
    tail--;

    inst->setInROB();

    ++numInstsInROB;
    ++threadEntries[tid];

    assert((*tail) == inst);

    DPRINTF(ROB, "[tid:%i] Now has %d instructions.\n", tid, threadEntries[tid]);

}

template <class Impl>
void
ROB<Impl>::retireHead(ThreadID tid)
{
    robWrites++;

    assert(numInstsInROB > 0);

    // Get the head ROB instruction.
    InstIt head_it = instList[tid].begin();

    DynInstPtr head_inst = (*head_it);

    assert(head_inst->readyToCommit());

    DPRINTF(ROB, "[tid:%u]: Retiring head instruction, "
            "instruction PC %s, [sn:%lli]\n", tid, head_inst->pcState(),
            head_inst->seqNum);

    --numInstsInROB;
    --threadEntries[tid];

    head_inst->clearInROB();
    head_inst->setCommitted();

    instList[tid].erase(head_it);

    /*** [Jiyong,STT] add logic for clearing argProducers ***/
    for (auto nextInstIt = std::next(instList[tid].begin()); nextInstIt != instList[tid].end(); nextInstIt++){
        // find matched physical reg between head_inst and next instr
        DynInstPtr nextInst = (*nextInstIt);
        for (int i = 0; i < nextInst->numSrcRegs(); i++){
            if (nextInst->getArgProducer(i) == head_inst)
                nextInst->clearArgProducer(i);
        }
    }

    // clear argProducer for head_inst
    for (int i = 0; i < head_inst->numSrcRegs(); i++)
        head_inst->clearArgProducer(i);

    //Update "Global" Head of ROB
    updateHead();

    // @todo: A special case is needed if the instruction being
    // retired is the only instruction in the ROB; otherwise the tail
    // iterator will become invalidated.
    cpu->removeFrontInst(head_inst);
}

template <class Impl>
bool
ROB<Impl>::isHeadReady(ThreadID tid)
{
    robReads++;
    if (threadEntries[tid] != 0) {
        return (instList[tid].front()->readyToCommit() &
                instList[tid].front()->isLoadSafeToCommit());
    }

    return false;
}

template <class Impl>
bool
ROB<Impl>::canCommit()
{
    //@todo: set ActiveThreads through ROB or CPU
    list<ThreadID>::iterator threads = activeThreads->begin();
    list<ThreadID>::iterator end = activeThreads->end();

    while (threads != end) {
        ThreadID tid = *threads++;

        if (isHeadReady(tid)) {
            return true;
        }
    }

    return false;
}

template <class Impl>
unsigned
ROB<Impl>::numFreeEntries()
{
    return numEntries - numInstsInROB;
}

template <class Impl>
unsigned
ROB<Impl>::numFreeEntries(ThreadID tid)
{
    return maxEntries[tid] - threadEntries[tid];
}

template <class Impl>
void
ROB<Impl>::doSquash(ThreadID tid)
{
    robWrites++;
    DPRINTF(ROB, "[tid:%u]: Squashing instructions until [sn:%i].\n",
            tid, squashedSeqNum[tid]);

    assert(squashIt[tid] != instList[tid].end());

    if ((*squashIt[tid])->seqNum < squashedSeqNum[tid]) {
        DPRINTF(ROB, "[tid:%u]: Done squashing instructions.\n",
                tid);

        squashIt[tid] = instList[tid].end();

        doneSquashing[tid] = true;
        return;
    }

    bool robTailUpdate = false;

    for (int numSquashed = 0;
         numSquashed < squashWidth &&
         squashIt[tid] != instList[tid].end() &&
         (*squashIt[tid])->seqNum > squashedSeqNum[tid];
         ++numSquashed)
    {
        DPRINTF(ROB, "[tid:%u]: Squashing instruction PC %s, seq num %i.\n",
                (*squashIt[tid])->threadNumber,
                (*squashIt[tid])->pcState(),
                (*squashIt[tid])->seqNum);

        // Mark the instruction as squashed, and ready to commit so that
        // it can drain out of the pipeline.
        (*squashIt[tid])->setSquashed();

        (*squashIt[tid])->hasPendingSquash(false);

        (*squashIt[tid])->setCanCommit();


        if (squashIt[tid] == instList[tid].begin()) {
            DPRINTF(ROB, "Reached head of instruction list while "
                    "squashing.\n");

            squashIt[tid] = instList[tid].end();

            doneSquashing[tid] = true;

            return;
        }

        InstIt tail_thread = instList[tid].end();
        tail_thread--;

        if ((*squashIt[tid]) == (*tail_thread))
            robTailUpdate = true;

        squashIt[tid]--;
    }


    // Check if ROB is done squashing.
    if ((*squashIt[tid])->seqNum <= squashedSeqNum[tid]) {
        DPRINTF(ROB, "[tid:%u]: Done squashing instructions.\n",
                tid);

        squashIt[tid] = instList[tid].end();

        doneSquashing[tid] = true;
    }

    if (robTailUpdate) {
        updateTail();
    }
}


/* **************************
 * [SafeSpec] update load insts state
 * isPrevInstsCompleted; isPrevBrsResolved
 * *************************/
template <class Impl>
void
ROB<Impl>::updateVisibleState()
{
    list<ThreadID>::iterator threads = activeThreads->begin();
    list<ThreadID>::iterator end = activeThreads->end();

    while (threads != end) {
        ThreadID tid = *threads++;

        if (instList[tid].empty())
            continue;

        InstIt inst_it = instList[tid].begin();
        InstIt tail_inst_it = instList[tid].end();

        bool prevInstsComplete=true;
        bool prevBrsResolved=true;
        bool prevInstsCommitted=true;
        bool prevBrsCommitted=true;

        while (inst_it != tail_inst_it) {
            DynInstPtr inst = *inst_it++;

            assert(inst!=0);

            if (!prevInstsComplete &&
                    !prevBrsResolved) {
                break;
            }

            //if (inst->isLoad()) {
                if (prevInstsComplete) {
                    inst->setPrevInstsCompleted();
                }
                if (prevBrsResolved){
                    inst->setPrevBrsResolved();
                }
                if (prevInstsCommitted) {
                    inst->setPrevInstsCommitted();
                }
                if (prevBrsCommitted) {
                    inst->setPrevBrsCommitted();
                }
            //}

            // Update prev control insts state
            if (inst->isControl()){
                prevBrsCommitted = false;
                if (!inst->readyToCommit() || inst->getFault()!=NoFault
                        || inst->isSquashed()){
                    prevBrsResolved = false;
                }
            }

            prevInstsCommitted = false;

            // Update prev insts state
            if (inst->isNonSpeculative() || inst->isStoreConditional()
               || inst->isMemBarrier() || inst->isWriteBarrier() ||
               (inst->isLoad() && inst->strictlyOrdered())){
                //Some special instructions, directly set canCommit
                //when entering ROB
                prevInstsComplete = false;
            }
            if ( !(inst->readyToCommit() & inst->isLoadSafeToCommit())
                    || inst->getFault()!=NoFault
                    || inst->isSquashed()){
                prevInstsComplete = false;
            }

            /*** [Jiyong, STT] add logic for updating flags when apply STT ***/
            if (cpu->protectionEnabled && !cpu->isInvisibleSpec){
                // fence
                //if ((cpu->isFuturistic && inst->isPrevInstsCommitted()) ||
                    //(!cpu->isFuturistic && inst->isPrevBrsCommitted())){
                if ((cpu->isFuturistic && inst->isPrevInstsCompleted()) ||
                    (!cpu->isFuturistic && inst->isPrevBrsResolved())){
                    inst->isUnsquashable(true);
                } else {
                    inst->isUnsquashable(false);
                }
            }
            else if (cpu->protectionEnabled && cpu->isInvisibleSpec) {
                // invisiSpec
                if ((cpu->isFuturistic && inst->isPrevInstsCompleted()) ||
                    (!cpu->isFuturistic && inst->isPrevBrsResolved())) {
                    inst->isUnsquashable(true);
                } else {
                    inst->isUnsquashable(false);
                }
            }
            else {
                // unsafebaseline
                inst->isUnsquashable(true);
            }
        }
    }
}


template <class Impl>
void
ROB<Impl>::updateHead()
{
    InstSeqNum lowest_num = 0;
    bool first_valid = true;

    // @todo: set ActiveThreads through ROB or CPU
    list<ThreadID>::iterator threads = activeThreads->begin();
    list<ThreadID>::iterator end = activeThreads->end();

    while (threads != end) {
        ThreadID tid = *threads++;

        if (instList[tid].empty())
            continue;

        if (first_valid) {
            head = instList[tid].begin();
            lowest_num = (*head)->seqNum;
            first_valid = false;
            continue;
        }

        InstIt head_thread = instList[tid].begin();

        DynInstPtr head_inst = (*head_thread);

        assert(head_inst != 0);

        if (head_inst->seqNum < lowest_num) {
            head = head_thread;
            lowest_num = head_inst->seqNum;
        }
    }

    if (first_valid) {
        head = instList[0].end();
    }

}

template <class Impl>
void
ROB<Impl>::updateTail()
{
    tail = instList[0].end();
    bool first_valid = true;

    list<ThreadID>::iterator threads = activeThreads->begin();
    list<ThreadID>::iterator end = activeThreads->end();

    while (threads != end) {
        ThreadID tid = *threads++;

        if (instList[tid].empty()) {
            continue;
        }

        // If this is the first valid then assign w/out
        // comparison
        if (first_valid) {
            tail = instList[tid].end();
            tail--;
            first_valid = false;
            continue;
        }

        // Assign new tail if this thread's tail is younger
        // than our current "tail high"
        InstIt tail_thread = instList[tid].end();
        tail_thread--;

        if ((*tail_thread)->seqNum > (*tail)->seqNum) {
            tail = tail_thread;
        }
    }
}


template <class Impl>
void
ROB<Impl>::squash(InstSeqNum squash_num, ThreadID tid)
{
    if (isEmpty(tid)) {
        DPRINTF(ROB, "Does not need to squash due to being empty "
                "[sn:%i]\n",
                squash_num);

        return;
    }

    DPRINTF(ROB, "Starting to squash within the ROB.\n");

    robStatus[tid] = ROBSquashing;

    doneSquashing[tid] = false;

    squashedSeqNum[tid] = squash_num;

    if (!instList[tid].empty()) {
        InstIt tail_thread = instList[tid].end();
        tail_thread--;

        squashIt[tid] = tail_thread;

        doSquash(tid);
    }
}

template <class Impl>
typename Impl::DynInstPtr
ROB<Impl>::readHeadInst(ThreadID tid)
{
    if (threadEntries[tid] != 0) {
        InstIt head_thread = instList[tid].begin();

        assert((*head_thread)->isInROB());

        return *head_thread;
    } else {
        return dummyInst;
    }
}

template <class Impl>
typename Impl::DynInstPtr
ROB<Impl>::readTailInst(ThreadID tid)
{
    InstIt tail_thread = instList[tid].end();
    tail_thread--;

    return *tail_thread;
}

template <class Impl>
void
ROB<Impl>::regStats()
{
    using namespace Stats;
    robReads
        .name(name() + ".rob_reads")
        .desc("The number of ROB reads");

    robWrites
        .name(name() + ".rob_writes")
        .desc("The number of ROB writes");
}

template <class Impl>
typename Impl::DynInstPtr
ROB<Impl>::findInst(ThreadID tid, InstSeqNum squash_inst)
{
    for (InstIt it = instList[tid].begin(); it != instList[tid].end(); it++) {
        if ((*it)->seqNum == squash_inst) {
            return *it;
        }
    }
    return NULL;
}

/*
 * [Jiyong, STT] routines for STT
 */
template <class Impl>
void
ROB<Impl>::explicit_flow(ThreadID tid, InstIt instIt)
{
    DynInstPtr inst = (*instIt);
    for (int i = 0; i < inst->numSrcRegs(); i++){
        if (inst->getArgProducer(i) != DynInstPtr()){
            DynInstPtr argProducer = inst->getArgProducer(i);
            assert(argProducer->threadNumber == tid);
            if (argProducer->isDestTainted()
                && !argProducer->isCommitted()) {
                inst->hasExplicitFlow(true);
                return;
            }
        }
    }
    inst->hasExplicitFlow(false);
    return;
}

template <class Impl>
void
ROB<Impl>::address_flow(ThreadID tid, InstIt instIt)
{
    DynInstPtr inst = (*instIt);
    if (inst->isMemRef()) {
        if (inst->isStore()) {
            for (int i = 1; i < inst->numSrcRegs(); i++){
                if (inst->getArgProducer(i) != DynInstPtr()){
                    DynInstPtr argProducer = inst->getArgProducer(i);
                    assert(argProducer->threadNumber == tid);
                    if (argProducer->isDestTainted()
                        && !argProducer->isCommitted()) {
                        inst->isAddrTainted(true);
                        return;
                    }
                }
            }
        }
        else if (inst->isLoad()) {
            for (int i = 0; i < inst->numSrcRegs(); i++){
                if (inst->getArgProducer(i) != DynInstPtr()){
                    DynInstPtr argProducer = inst->getArgProducer(i);
                    assert(argProducer->threadNumber == tid);
                    if (argProducer->isDestTainted()
                        && !argProducer->isCommitted()) {
                        inst->isAddrTainted(true);
                        return;
                    }
                }
            }
        }
        else {
            printf("Unidentified instruction.\n");
            print_robs();
            assert (0);
        }

        inst->isAddrTainted(false);
    } else {
        inst->isAddrTainted(false);
    }
}

template <class Impl>
void
ROB<Impl>::implicit_flow(ThreadID tid, InstIt instIt)
{
    DynInstPtr inst = (*instIt);
    if (cpu->impChannel) {
        for (auto prevInstIt = instList[tid].begin(); prevInstIt != instIt; prevInstIt++) {
            DynInstPtr prevInst = (*prevInstIt);
            if (prevInst->isControl() && prevInst->hasExplicitFlow()) {
                inst->hasImplicitFlow(true);
                return;
            }
        }
    }
    inst->hasImplicitFlow(false);
    return;
}

template <class Impl>
void
ROB<Impl>::compute_taint()
{
    assert (cpu->STT);

    list<ThreadID>::iterator threads = activeThreads->begin();
    list<ThreadID>::iterator end = activeThreads->end();

    while(threads != end) {
        ThreadID tid = *threads++;
        if (instList[tid].empty())
            continue;

        for (auto instIt = instList[tid].begin(); instIt != instList[tid].end(); instIt++) {
            explicit_flow(tid, instIt);
            implicit_flow(tid, instIt);
            address_flow(tid, instIt);
            DynInstPtr inst = (*instIt);

            inst->isArgsTainted(inst->hasExplicitFlow());

            inst->isDestTainted(inst->isArgsTainted());
            if (inst->isAccess() && !inst->isUnsquashable()) {
                inst->isDestTainted(true);
            }
        }
    }
}

template <class Impl>
void
ROB<Impl>::print_robs()
{
    list<ThreadID>::iterator threads = activeThreads->begin();
    list<ThreadID>::iterator end = activeThreads->end();

    while(threads != end) {
        ThreadID tid = *threads++;
        printf("\nROB for thread %d\n", tid);

        for(int i = 0; i < 50; i++)
            printf("-");
        printf("\n");

        for (auto instIt = instList[tid].begin(); instIt != instList[tid].end(); instIt++) {
            auto inst = (*instIt);
            printf("ptr=%p, [sn:%ld], inst=%s ", inst.get(), inst->seqNum,
                    inst->staticInst->getName().c_str());
            for (int j = 0; j < inst->numDestRegs(); j++)
                printf("%d(%s), ", inst->destRegIdx(j).index(), inst->destRegIdx(j).className());
            printf("| ");
            for (int j = 0; j < inst->numSrcRegs(); j++)
                printf("%d(%s), ", inst->srcRegIdx(j).index(), inst->srcRegIdx(j).className());
            printf("| ");

            for (int j = 0; j < inst->numDestRegs(); j++)
                printf("destPhys[%d] = %d(%d), ", j, inst->renamedDestRegIdx(j)->index(), inst->renamedDestRegIdx(j)->flatIndex());
            for (int j = 0; j < inst->numSrcRegs(); j++)
                printf("srcPhys[%d] = %d(%d), ", j, inst->renamedSrcRegIdx(j)->index(), inst->renamedSrcRegIdx(j)->flatIndex());
            printf("fenceDelay=%d, ", inst->fenceDelay());
            printf("squash=%d, fault?=%d, ", inst->isSquashed(), inst->getFault() != NoFault);
            printf("pendingSquash?=%d, ", inst->hasPendingSquash());
            printf("cancommit=%d, ", inst->checkCanCommit());
            printf("status=");
            if (inst->isCommitted())
                printf("Committed, ");
            else if (inst->readyToCommit()){
                if (inst->isExecuted())
                    printf("CanCommit(Exec), ");
                else
                    printf("CanCommit(NonExec), ");
            }
            else if (inst->isExecuted())
                printf("Executed, ");
            else if (inst->isIssued())
                printf("Issued, ");
            else
                printf("Not Issued, ");
            printf("unsquashable=%d, DestTainted=%d, ArgsTainted=%d, ", inst->isUnsquashable(), inst->isDestTainted(), inst->isArgsTainted());
            printf("PBR=%d, PBC=%d, PIR=%d, PIC=%d, ", inst->isPrevBrsResolved(), inst->isPrevBrsCommitted(), inst->isPrevInstsCompleted(), inst->isPrevInstsCommitted());
            for(int j = 0; j < inst->numSrcRegs(); j++){
                printf("Producer[%d] = %p ", j, inst->getArgProducer(j).get());
                if (inst->getArgProducer(j) != DynInstPtr())
                    printf("[sn:%ld], ", inst->getArgProducer(j)->seqNum);
            }
            if (inst->numDestRegs() > 1){
                printf("%d, %d, %d, %d, %d", inst->numFPDestRegs(), inst->numIntDestRegs(), inst->numCCDestRegs(), inst->numVecDestRegs(), inst->numVecElemDestRegs());
            }
            printf("\n");
            for(int i = 0; i < 50; i++)
                printf("-");
            printf("\n");
        }
    }
}


template <class Impl>
typename Impl::DynInstPtr
ROB<Impl>::getResolvedPendingSquashInst(ThreadID tid)
{
    for (auto instIt = instList[tid].begin(); instIt != instList[tid].end(); instIt++) {
        auto inst = (*instIt);
        if (inst->hasPendingSquash()
            && !inst->isArgsTainted()
            && !inst->isSquashed()  // if it's already squashed, we ignore it
            ) {
            return inst;
        }
    }
    return NULL;
}

#endif//__CPU_O3_ROB_IMPL_HH__
