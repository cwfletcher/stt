# Speculative Taint Tracking (STT)

## 1. About STT

Speculative taint tracking (STT) is a hardware defense mechanism for blocking all types of speculative execution attacks in modern processors. All details can be found in our MICRO'19 paper [here](dl.acm.org/citation.cfm?id=3358274). Here is a sample format for citing our work:
```
@inproceedings{yu2019stt,
  title={Speculative Taint Tracking (STT) A Comprehensive Protection for Speculatively Accessed Data},
  author={Yu, Jiyong and Yan, Mengjia and Khyzha, Artem and Morrison, Adam and Torrellas, Josep and Fletcher, Christopher W},
  booktitle={Proceedings of the 52nd Annual IEEE/ACM International Symposium on Microarchitecture},
  pages={954--968},
  year={2019}
}
```

## 2. Implementation

We implement STT using Gem5 simulator. This is built on an early version of Gem5 (commit:38a1e23). To make the simulation close to a commodity processor, we use Gem5's o3 processor. The major changes are:

* add taint tracking logic to track all tainted data
* add delay logic for handling explicit channels (memory instructions)
* add delay logic for handling implicit channels (branch prediction, memory speculation, ld-st forwarding)

## 3. Usage

### 1) Follow the steps for building Gem5 executable.
How to use Gem5 can be found [here](gem5.org).

### 2) We add the following configurations for STT:
* --threat_model [string]: different threat models
    * UnsafeBaseline: unmodified out-of-order processor without protection
    * Spectre: Spectre threat model (covering control-flow speculation)
    * Futuristic: Futuristic threat model (covering all types speculation, exceptions, interrupts)

* --needsTSO [bool]: configure the consistency model
    * True: use Total Store Ordering (TSO) model
    * False: use Relaxed Consistency (RC) model

* --STT [int]: configure STT
    * 0: disable STT (in this case, the defense scheme blocks all speculative transmitters)
    * 1: enable STT

* --implicit_channel [int]: configure implicit channel protection
    * 0: ignore implicit channels
    * 1: enable protection against implicit channels

### 3) Sample scripts
We have a few sample scripts in './sample_scripts'.
