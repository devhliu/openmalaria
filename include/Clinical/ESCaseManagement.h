/*
 This file is part of OpenMalaria.
 
 Copyright (C) 2005-2009 Swiss Tropical Institute and Liverpool School Of Tropical Medicine
 
 OpenMalaria is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or (at
 your option) any later version.
 
 This program is distributed in the hope that it will be useful, but
 WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 General Public License for more details.
 
 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
*/

#ifndef Hmod_ESCaseManagement
#define Hmod_ESCaseManagement

#include "Global.h"
#include "Clinical/ESDecision.h"
#include "Pathogenesis/State.h"
#include "WithinHost/WithinHostModel.h"
#include "Survey.h"
#include "inputData.h"

#include <cassert>
#include <list>
#include <map>


namespace OM { namespace Clinical {

/// Data used for a withinHostModel->medicate() call
struct MedicateData {
    /// Checkpointing
    template<class S>
    void operator& (S& stream) {
	abbrev & stream;
	qty & stream;
	time & stream;
	seekingDelay & stream;
    }
    
    string abbrev;	/// Drug abbreviation
    double qty;		/// Quantity of drug prescribed
    int time;		/// Time of day to medicate at (minutes from start)
    //FIXME: this should be total days delay; time should always be <24*60
    int seekingDelay;	/// Delay before treatment seeking in days
};

/// Data type stored in decisions
/// TODO: treatment seeking delay(?), hospital/community care, RDTs or not.
struct CaseTreatment {
    CaseTreatment (const scnXml::CM_leaf::MedicateSequence mSeq) {
	medications.resize (mSeq.size ());
	for (size_t j = 0; j < mSeq.size(); ++j) {
	    medications[j].abbrev = mSeq[j].getName();
	    medications[j].qty = mSeq[j].getQty();
	    medications[j].time = mSeq[j].getTime();
	}
    }
    
    /// Add medications into medicate queue
    inline void apply (list<MedicateData>& medicateQueue, cmid id) {
	// Extract treatment-seeking delay from id (branch of our case-management tree)
	int delay = (id & Decision::TSDELAY_MASK) >> Decision::TSDELAY_SHIFT;
	assert (delay <= Decision::TSDELAY_NUM_MAX);
	
	for (vector<MedicateData>::iterator it = medications.begin(); it != medications.end(); ++it) {
	    medicateQueue.push_back (*it);
	    medicateQueue.back().seekingDelay = delay;
	}
    }
    
    /// Data for each medicate() call.
    vector<MedicateData> medications;
};

/** Tracks clinical status (sickness), does case management for new events,
 * medicates treatment, determines patient recovery, death and sequelae.
 */
class ESCaseManagement {
    public:
	static void init ();
	
	static cmid execute (list<MedicateData>& medicateQueue, Pathogenesis::State pgState, WithinHost::WithinHostModel& withinHostModel, double ageYears, SurveyAgeGroup ageGroup);
	
    private:
	static pair<cmid,CaseTreatment&> traverse (cmid id);
	
	class CMNode {
	    public:
		virtual pair<cmid,CaseTreatment&> traverse (cmid id) =0;
	};
	class CMPBranchSet : public CMNode {
	    struct PBranch {
		cmid outcome;
		double cumP;
	    };
	    vector<PBranch> branches;	// must contain at least one entry; last must have cumP => 1.0
	    
	    public:
		CMPBranchSet (const scnXml::CM_pBranchSet::CM_pBranchSequence& branchSeq);
		
		virtual pair<cmid,CaseTreatment&> traverse (cmid id);
	};
	class CMLeaf : public CMNode {
	    CaseTreatment ct;
	    public:
		CMLeaf (CaseTreatment t) : ct(t) {}
		
		virtual pair<cmid,CaseTreatment&> traverse (cmid id);
	};
	
	//FIXME: use hash-map instead
	//BEGIN Static parameters — set by init()
	typedef map<cmid,CMNode*> TreeType;
	/// Tree probability-branches and leaf nodes.
	static TreeType cmTree;
	
	/// Mask applied to id before lookup in cmTree.
	static cmid cmMask;
	//END
};

} }
#endif