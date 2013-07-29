/* This file is part of OpenMalaria.
 * 
 * Copyright (C) 2005-2013 Swiss Tropical and Public Health Institute 
 * Copyright (C) 2005-2013 Liverpool School Of Tropical Medicine
 * 
 * OpenMalaria is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include "Interventions.h"
#include "Host/Vaccine.h"
#include "Population.h"
#include "util/random.h"
#include "Clinical/ESCaseManagement.h"
#include "Clinical/ImmediateOutcomes.h"
#include "WithinHost/DescriptiveIPTWithinHost.h"
#include "Clinical/CaseManagementCommon.h"
#include "Monitoring/Surveys.h"

namespace OM {
    using Host::Human;

// ———  ContinuousDeployment and derivatives  ———

ContinuousDeployment::ContinuousDeployment(
        const ::scnXml::ContinuousDeployment& elt ) :
    begin( elt.getBegin() ),
    end( elt.getEnd() ),
    deployAge( TimeStep::fromYears( elt.getTargetAgeYrs() ) ),
    cohortOnly( elt.getCohort() ),
    coverage( elt.getCoverage() )
{
    if( begin < TimeStep(0) || end < begin ){
        throw util::xml_scenario_error("continuous intervention must have 0 <= begin <= end");
    }
    if( deployAge <= TimeStep(0) ){
        ostringstream msg;
        msg << "continuous intervention with target age "<<elt.getTargetAgeYrs();
        msg << " years corresponds to timestep "<<deployAge;
        msg << "; must be at least timestep 1.";
        throw util::xml_scenario_error( msg.str() );
    }
    if( deployAge > TimeStep::maxAgeIntervals ){
        ostringstream msg;
        msg << "continuous intervention must have target age no greater than ";
        msg << TimeStep::maxAgeIntervals * TimeStep::yearsPerInterval;
        throw util::xml_scenario_error( msg.str() );
    }
    if( !(coverage >= 0.0 && coverage <= 1.0) ){
        throw util::xml_scenario_error("continuous intervention coverage must be in range [0,1]");
    }
}

bool ContinuousDeployment::filterAndDeploy( Host::Human& human, const Population& population )const{
    TimeStep age = TimeStep::simulation - human.getDateOfBirth();
    if( deployAge > age ){
        // stop processing continuous deployments for this
        // human for now because remaining ones happen in the future
        return false;
    }else if( deployAge == age ){
        if( begin <= TimeStep::interventionPeriod &&
            TimeStep::interventionPeriod < end &&
            ( !cohortOnly || human.isInCohort() ) &&
            util::random::uniform_01() < coverage )     // RNG call should be last test
        {
            deploy( human, population );
        }
    }//else: for some reason, a deployment age was missed; ignore it
    return true;
}

/** Age-based (continuous) deployment. */
//TODO: phase out usage of this
class AgeBasedDeployment : public ContinuousDeployment {
public:
    AgeBasedDeployment( const ::scnXml::ContinuousDeployment& elt,
            void(Host::Human::*func) (const OM::Population&) ) :
        ContinuousDeployment(elt),
        deployFn( func )
    {
    }
    
protected:
    virtual void deploy( Host::Human& human, const Population& population )const{
        (human.*deployFn)( population );
    }
    
    // Member function pointer to the function (in Human) responsible for deploying intervention:
    typedef void (Host::Human::*DeploymentFunction) (const OM::Population&);
    DeploymentFunction deployFn;
};

/** Age-based deployment for new list-of-effect interventions. */
class ContinuousHumanIntervention : public ContinuousDeployment {
public:
    ContinuousHumanIntervention( const scnXml::ContinuousDeployment& elt,
                                 const HumanIntervention* intervention ) :
        ContinuousDeployment( elt ),
        intervention( intervention )
    {
    }
    
protected:
    virtual void deploy( Host::Human& human, const Population& population )const{
        intervention->deploy( human, Deployment::CTS );
    }
    
    const HumanIntervention *intervention;
};


// ———  TimedDeployment and derivatives  ———

TimedDeployment::TimedDeployment(TimeStep deploymentTime) :
    time( deploymentTime )
{
    if( deploymentTime < TimeStep(0) ){
        throw util::xml_scenario_error("timed intervention deployment: may not be negative");
    }else if( deploymentTime >= Monitoring::Surveys.getFinalTimestep() ){
        cerr << "Warning: timed intervention deployment at time "<<deploymentTime.asInt();
        cerr << " happens after last survey" << endl;
    }
}

class DummyTimedDeployment : public TimedDeployment {
public:
    DummyTimedDeployment() :
        TimedDeployment( TimeStep(0) )
    {
        // TimedDeployment's ctor checks that the deployment time-step is
        // within the intervention period. We want this time to be after the
        // last time-step, so set the time here after TimedDeployment's ctor
        // check has been done (hacky).
        time = TimeStep::future;
    }
    virtual void deploy (OM::Population&) {}
};

class TimedChangeHSDeployment : public TimedDeployment {
public:
    TimedChangeHSDeployment( const scnXml::ChangeHS::TimedDeploymentType& hs ) :
        TimedDeployment( TimeStep( hs.getTime() ) ),
        newHS( hs._clone() )
    {}
    virtual void deploy (OM::Population& population) {
        Clinical::CaseManagementCommon::changeHealthSystem( *newHS );
        delete newHS;
        newHS = 0;
    }
    
private:
    scnXml::HealthSystem *newHS;
};

class TimedChangeEIRDeployment : public TimedDeployment {
public:
    TimedChangeEIRDeployment( const scnXml::ChangeEIR::TimedDeploymentType& nv ) :
        TimedDeployment( TimeStep( nv.getTime() ) ),
        newEIR( nv._clone() )
    {}
    virtual void deploy (OM::Population& population) {
        population.transmissionModel().changeEIRIntervention( *newEIR );
        delete newEIR;
        newEIR = 0;
    }
    
private:
    scnXml::NonVector *newEIR;
};

class TimedUninfectVectorsDeployment : public TimedDeployment {
public:
    TimedUninfectVectorsDeployment( TimeStep deployTime ) :
        TimedDeployment( deployTime )
    {}
    virtual void deploy (OM::Population& population) {
        population.transmissionModel().uninfectVectors();
    }
};

class TimedR_0Deployment : public TimedDeployment {
public:
    TimedR_0Deployment( TimeStep deployTime ) :
        TimedDeployment( deployTime )
    {}
    virtual void deploy (OM::Population& population) {
        int i = (int)std::floor (util::random::uniform_01() * population.getSize());        // pick a human
        Population::HumanIter it = population.getList().begin();
        while (i > 0){  // find human (can't use population[i])
            ++it;
            --i;
        }
        assert( i == 0 );
        assert( it != population.getList().end() );
        it->R_0Vaccines();
        it->addInfection();
    }
};

/// Deployment of mass-to-human interventions (TODO: phase out usages of this)
class TimedMassDeployment : public TimedDeployment {
public:
    /** 
     * @param mass XML element specifying the age range and compliance
     * (proportion of eligible individuals who receive the intervention).
     * @param deployIntervention A member-function pointer to a
     *      "void func (const OM::Population&)" function within human which
     *      activates the intervention. Population is passed for acces to static
     *      params. */
    TimedMassDeployment( const scnXml::Mass& mass,
                           void (Host::Human::*deployIntervention)(const OM::Population&) ) :
        TimedDeployment( TimeStep( mass.getTime() ) ),
        minAge( TimeStep::fromYears( mass.getMinAge() ) ),
        maxAge( TimeStep::fromYears( mass.getMaxAge() ) ),
        cohortOnly( mass.getCohort() ),
        coverage( mass.getCoverage() ),
        intervention( deployIntervention )
    {
        if( !(coverage >= 0.0 && coverage <= 1.0) ){
            throw util::xml_scenario_error("timed intervention coverage must be in range [0,1]");
        }
        if( minAge < TimeStep(0) || maxAge < minAge ){
            throw util::xml_scenario_error("timed intervention must have 0 <= minAge <= maxAge");
        }
    }
    
    virtual void deploy (OM::Population& population) {
        Population::HumanPop& popList = population.getList();
        for (Population::HumanIter iter = popList.begin(); iter != popList.end(); ++iter) {
            TimeStep age = TimeStep::simulation - iter->getDateOfBirth();
            if( age >= minAge && age < maxAge ){
                if( !cohortOnly || iter->isInCohort() ){
                    if( util::random::uniform_01() < coverage ){
                        // This is UGLY syntax. It just means call intervention() on the human pointed by iter.
                        ( (*iter).*intervention) (population);
                    }
                }
            }
        }
    }
    
protected:
    // restrictions on deployment
    TimeStep minAge;
    TimeStep maxAge;
    bool cohortOnly;
    double coverage;    // proportion coverage within group meeting above restrictions
    void (Host::Human::*intervention) (const OM::Population&);       // callback: per-human deployment
};

/// Timed deployment of human-specific interventions
class TimedHumanDeployment : public TimedDeployment {
public:
    /** 
     * @param mass XML element specifying the age range and compliance
     * (proportion of eligible individuals who receive the intervention).
     * @param intervention The HumanIntervention to deploy. */
    TimedHumanDeployment( const scnXml::Mass& mass,
                           const HumanIntervention* intervention ) :
        TimedDeployment( TimeStep( mass.getTime() ) ),
        minAge( TimeStep::fromYears( mass.getMinAge() ) ),
        maxAge( TimeStep::fromYears( mass.getMaxAge() ) ),
        cohortOnly( mass.getCohort() ),
        coverage( mass.getCoverage() ),
        intervention( intervention )
    {
        if( !(coverage >= 0.0 && coverage <= 1.0) ){
            throw util::xml_scenario_error("timed intervention coverage must be in range [0,1]");
        }
        if( minAge < TimeStep(0) || maxAge < minAge ){
            throw util::xml_scenario_error("timed intervention must have 0 <= minAge <= maxAge");
        }
    }
    
    virtual void deploy (OM::Population& population) {
        Population::HumanPop& popList = population.getList();
        for (Population::HumanIter iter = popList.begin(); iter != popList.end(); ++iter) {
            TimeStep age = TimeStep::simulation - iter->getDateOfBirth();
            if( age >= minAge && age < maxAge ){
                if( !cohortOnly || iter->isInCohort() ){
                    if( util::random::uniform_01() < coverage ){
                        intervention->deploy( *iter, Deployment::TIMED );
                    }
                }
            }
        }
    }
    
protected:
    // restrictions on deployment
    TimeStep minAge;
    TimeStep maxAge;
    bool cohortOnly;
    double coverage;    // proportion coverage within group meeting above restrictions
    const HumanIntervention *intervention;
};

/// Deployment of mass-to-human interventions with cumulative-deployment support (TODO: phase out usages of this)
class TimedMassCumDeployment : public TimedMassDeployment {
public:
    /** As massIntervention, but supports "increase to target coverage" mode:
     * Deployment is only to unprotected humans and brings
     * total coverage up to the level given in description.
     * 
     * @param mass XML element specifying the age range and compliance
     * (proportion of eligible individuals who receive the intervention).
     * @param deployIntervention A member-function pointer to a
     *      "void func (const OM::Population&)" function within human which
     *      activates the intervention. Population is passed for acces to static
     *      params.
     * @param isProtectedCb A member-function pointer to a
     * "bool func (TimeStep maxAge)" function on a Human which returns true if
     * the Human is still protected by an intervention of the type in question
     * which is no older than maxAge. */
    TimedMassCumDeployment( const scnXml::MassCum& mass,
                              void (Host::Human::*deployIntervention)(const OM::Population&),
                              bool (Host::Human::*isProtectedCb) (TimeStep) const ) :
        TimedMassDeployment( mass, deployIntervention ),
        isProtected( isProtectedCb ),
        maxInterventionAge( TimeStep::fromYears( mass.getCumulativeWithMaxAge().get() ) )
    {}
    
    void deploy(OM::Population& population){
        // Cumulative case: bring target group's coverage up to target coverage
        Population::HumanPop& popList = population.getList();
        vector<Host::Human*> unprotected;
        size_t total = 0;       // number of humans within age bound and optionally cohort
        for (Population::HumanIter iter = popList.begin(); iter != popList.end(); ++iter) {
            TimeStep age = TimeStep::simulation - iter->getDateOfBirth();
            if( age >= minAge && age < maxAge ){
                if( !cohortOnly || iter->isInCohort() ){
                    total+=1;
                    if( !((*iter).*isProtected)(maxInterventionAge) )
                        unprotected.push_back( &*iter );
                }
            }
        }
        
        double propProtected = static_cast<double>( total - unprotected.size() ) / static_cast<double>( total );
        if( propProtected < coverage ){
            // Proportion propProtected are already covered, so need to
            // additionally cover the proportion (coverage - propProtected),
            // selected from the list unprotected.
            double additionalCoverage = (coverage - propProtected) / (1.0 - propProtected);
            for (vector<Host::Human*>::iterator iter = unprotected.begin();
                 iter != unprotected.end(); ++iter)
            {
                if( util::random::uniform_01() < additionalCoverage ){
                    ( (**iter).*intervention) (population);
                }
            }
        }
    }
    
private:
    // callback to ascertain whether a human is still under protection from an
    // intervention young enough not to need replacement
    bool (Host::Human::*isProtected) (TimeStep) const;
    // max age at which an intervention is considered not to need replacement
    TimeStep maxInterventionAge;
};

class TimedVectorDeployment : public TimedDeployment {
public:
    TimedVectorDeployment( TimeStep deployTime, size_t instance ) :
        TimedDeployment( deployTime ),
        inst(instance)
    {}
    virtual void deploy (OM::Population& population) {
      population.transmissionModel().deployVectorPopInterv(inst);
    }
private:
    size_t inst;
};

/** Create either a TimedMassCumIntervention or a TimedMassIntervention,
 * depending on whether the cumulativeWithMaxAge attribute is present.
 * 
 * @param mass XML element specifying the age range and compliance
 * (proportion of eligible individuals who receive the intervention).
 * @param deployIntervention A member-function pointer to a
 *      "void func (const OM::Population&)" function within human which
 *      activates the intervention. Population is passed for acces to static params.
 * @param isProtectedCb A member-function pointer to a
 * "bool func (TimeStep maxAge)" function on a Human which returns true if
 * the Human is still protected by an intervention of the type in question
 * which is no older than maxAge.
 * 
 *  (TODO: phase out usages of this) */
TimedMassDeployment* createTimedMassCumIntervention(
    const scnXml::MassCum& mass,
    void (Host::Human::*deployIntervention)(const OM::Population&),
    bool (Host::Human::*isProtectedCb) (TimeStep) const
){
    if( mass.getCumulativeWithMaxAge().present() ){
        return new TimedMassCumDeployment( mass, deployIntervention, isProtectedCb );
    }else{
        return new TimedMassDeployment( mass, deployIntervention );
    }
}

// ———  HumanInterventionEffect  ———

void HumanIntervention::deploy( Human& human, Deployment::Method method ) const{
    for( vector<const HumanInterventionEffect*>::const_iterator it = effects.begin();
            it != effects.end(); ++it )
    {
        (*it)->deploy( human, method );
    }
}

class MDAEffect : public HumanInterventionEffect {
public:
    MDAEffect( const scnXml::MDA& mda ) {
        // Set description. TODO: allow multiple descriptions.
        if( TimeStep::interval == 5 ){
            if( !mda.getDiagnostic().present() ){
                // Note: allow no description for now to avoid XML changes.
                //throw util::xml_scenario_error( "error: interventions.MDA.diagnostic element required for MDA with 5-day timestep" );
                scnXml::HSDiagnostic diagnostic;
                scnXml::Deterministic det(0.0);
                diagnostic.setDeterministic(det);
                Clinical::ClinicalImmediateOutcomes::initMDA(diagnostic);
            }else{
                Clinical::ClinicalImmediateOutcomes::initMDA( mda.getDiagnostic().get() );
            }
        }else{
            if( !mda.getDescription().present() ){
                throw util::xml_scenario_error( "error: interventions.MDA.description element required for MDA with 1-day timestep" );
            }
            Clinical::ESCaseManagement::initMDA( mda.getDescription().get() );
        }
    }
    
    void deploy( Human& human, Deployment::Method method ) const{
        if( method != Deployment::TIMED )
            //TODO: easy, except reporting (and rename?)
            throw util::unimplemented_exception("MDA via cts deployment");
        human.massDrugAdministration();
    }
};

class VaccineEffect : public HumanInterventionEffect {
public:
    VaccineEffect( const scnXml::Vaccine::DescriptionSequence& seq ){
        //TODO: further revise vaccine description in XSD
        Host::Vaccine::initDescription( seq );
    }
    
    void deploy( Human& human, Deployment::Method method )const{
        human.deployVaccine( method );
    }
};

class IPTEffect : public HumanInterventionEffect {
public:
    IPTEffect( const scnXml::IPTDescription& elt ){
        WithinHost::DescriptiveIPTWithinHost::init( elt );
    }
    
    void deploy( Human& human, Deployment::Method method )const{
        human.deployIPT( method );
    }
};


// ———  InterventionManager  ———

InterventionManager::InterventionManager (const scnXml::Interventions& intervElt, OM::Population& population) :
    nextTimed(0), _cohortEnabled(false)
{
    if( intervElt.getChangeHS().present() ){
        const scnXml::ChangeHS& chs = intervElt.getChangeHS().get();
        if( chs.getTimedDeployment().size() > 0 ){
            // timed deployments:
            typedef scnXml::ChangeHS::TimedDeploymentSequence::const_iterator It;
            for( It it = chs.getTimedDeployment().begin(); it != chs.getTimedDeployment().end(); ++it ){
                timed.push_back( new TimedChangeHSDeployment( *it ) );
            }
        }
    }
    if( intervElt.getChangeEIR().present() ){
        const scnXml::ChangeEIR& eir = intervElt.getChangeEIR().get();
        if( eir.getTimedDeployment().size() > 0 ){
            // timed deployments:
            typedef scnXml::ChangeEIR::TimedDeploymentSequence::const_iterator It;
            for( It it = eir.getTimedDeployment().begin(); it != eir.getTimedDeployment().end(); ++it ){
                timed.push_back( new TimedChangeEIRDeployment( *it ) );
            }
        }
    }
    if( intervElt.getHuman().present() ){
        const scnXml::HumanInterventions& human = intervElt.getHuman().get();
        map<string,size_t> identifierMap;
        
        // 1. Read effects
        for( scnXml::HumanInterventions::EffectConstIterator it =
                human.getEffect().begin(), end = human.getEffect().end();
                it != end; ++it )
        {
            const scnXml::HumanInterventionEffect& effect = *it;
            identifierMap[effect.getId()] = humanEffects.size();        // i.e. index of next item
            if( effect.getMDA().present() ){
                humanEffects.push_back( new MDAEffect( effect.getMDA().get() ) );
            }else if( effect.getVaccine().present() ){
                //TODO
                humanEffects.push_back( new VaccineEffect( effect.getVaccine().get().getDescription() ) );
            }else if( effect.getIPT().present() ){
                humanEffects.push_back( new IPTEffect( effect.getIPT().get() ) );
            }else{
                throw util::xml_scenario_error(
                    "expected intervention.human.effect element to have a "
                    "child, didn't find it (perhaps I need updating)" );
            }
        }
        
        // 2. Read list of interventions
        for( scnXml::HumanInterventions::InterventionConstIterator it =
                human.getIntervention().begin(),
                end = human.getIntervention().end(); it != end; ++it )
        {
            bool hasVaccineEffect = false;      // for vaccine EPI deployment
            const scnXml::Intervention& elt = *it;
            // 2.a intervention effects
            HumanIntervention *intervention = new HumanIntervention();
            for( scnXml::Intervention::EffectConstIterator it2 = elt.getEffect().begin(),
                    end2 = elt.getEffect().end(); it2 != end2; ++it2 )
            {
                map<string,size_t>::const_iterator result = identifierMap.find( it2->getId() );
                if( result == identifierMap.end() ){
                    ostringstream msg;
                    msg << "human intervention references effect with id \""
                        << it2->getId()
                        << "\", but no effect with this id was found";
                    throw util::xml_scenario_error( msg.str() );
                }
                const HumanInterventionEffect* effect = &humanEffects[result->second];
                intervention->addEffect( effect );
                if( dynamic_cast<const VaccineEffect*>( effect ) != 0 ) hasVaccineEffect = true;
            }
            // 2.b intervention deployments
            if( elt.getContinuous().present() ){
                const scnXml::ContinuousList::DeploySequence& ctsSeq =
                        elt.getContinuous().get().getDeploy();
                for( scnXml::ContinuousList::DeployConstIterator it2 = ctsSeq.begin(),
                    end2 = ctsSeq.end(); it2 != end2; ++it2 )
                {
                    continuous.push_back( new ContinuousHumanIntervention( *it2, intervention ) );
                }
                if( hasVaccineEffect )
                    Host::Vaccine::initSchedule( ctsSeq );
            }
            if( elt.getTimed().present() ){
                const scnXml::MassListWithCum& timedElt = elt.getTimed().get();
                if( timedElt.getCumulativeCoverage().present() )
                    throw util::unimplemented_exception(
                        "cumulative coverage for human interventions" );
                for( scnXml::MassListWithCum::DeployConstIterator it2 =
                        timedElt.getDeploy().begin(), end2 =
                        timedElt.getDeploy().end(); it2 != end2; ++it2 )
                {
                    timed.push_back( new TimedHumanDeployment( *it2, intervention ) );
                }
            }
            humanInterventions.push_back( intervention );
        }
    }
    if( intervElt.getITN().present() ){
        const scnXml::ITN& itn = intervElt.getITN().get();
        if( itn.getTimed().present() || itn.getContinuous().present() ){
            // read description
            population.transmissionModel().setITNDescription( itn.getDescription() );
            // continuous deployments:
            if( itn.getContinuous().present() ){
                const scnXml::ContinuousList::DeploySequence& seq = itn.getContinuous().get().getDeploy();
                typedef scnXml::ContinuousList::DeploySequence::const_iterator CIt;
                for( CIt it = seq.begin(); it != seq.end(); ++it ){
                    continuous.push_back( new AgeBasedDeployment( *it, &Host::Human::ctsITN ) );
                }
            }
            // timed deployments:
            if( itn.getTimed().present() ){
                const scnXml::MassCumList::DeploySequence& seq = itn.getTimed().get().getDeploy();
                typedef scnXml::MassCumList::DeploySequence::const_iterator It;
                for( It it = seq.begin(); it != seq.end(); ++it ){
                    timed.push_back( createTimedMassCumIntervention( *it, &Host::Human::massITN, &Host::Human::hasITNProtection ) );
                }
            }
        }
    }
    if( intervElt.getIRS().present() ){
        const scnXml::IRS& irs = intervElt.getIRS().get();
        if( irs.getTimed().present() ){
            // read description
            population.transmissionModel().setIRSDescription( irs );
            // timed deployments:
            const scnXml::MassCumList::DeploySequence& seq = irs.getTimed().get().getDeploy();
            typedef scnXml::MassCumList::DeploySequence::const_iterator It;
            for( It it = seq.begin(); it != seq.end(); ++it ){
                timed.push_back( createTimedMassCumIntervention( *it, &Host::Human::massIRS, &Host::Human::hasIRSProtection ) );
            }
        }
    }
    if( intervElt.getVectorDeterrent().present() ){
        const scnXml::VectorDeterrent& va = intervElt.getVectorDeterrent().get();
        if( va.getTimed().present() ){
            // read description
            population.transmissionModel().setVADescription( va );
            // timed deployments:
            const scnXml::MassCumList::DeploySequence& seq = va.getTimed().get().getDeploy();
            typedef scnXml::MassCumList::DeploySequence::const_iterator It;
            for( It it = seq.begin(); it != seq.end(); ++it ){
                timed.push_back( createTimedMassCumIntervention( *it, &Host::Human::massVA, &Host::Human::hasVAProtection ) );
            }
        }
    }
    if( intervElt.getCohort().present() ){
        const scnXml::Cohort& ch = intervElt.getCohort().get();
        if( ch.getTimed().present() || ch.getContinuous().present() ){
            _cohortEnabled = true;
            // continuous deployments:
            if( ch.getContinuous().present() ){
                const scnXml::ContinuousList::DeploySequence& seq = ch.getContinuous().get().getDeploy();
                typedef scnXml::ContinuousList::DeploySequence::const_iterator CIt;
                for( CIt it = seq.begin(); it != seq.end(); ++it ){
                    continuous.push_back( new AgeBasedDeployment( *it, &Host::Human::addToCohort ) );
                }
            }
            // timed deployments:
            if( ch.getTimed().present() ){
                const scnXml::MassCumList::DeploySequence& seq = ch.getTimed().get().getDeploy();
                typedef scnXml::MassCumList::DeploySequence::const_iterator It;
                for( It it = seq.begin(); it != seq.end(); ++it ){
                    timed.push_back( createTimedMassCumIntervention( *it, &Host::Human::addToCohort, &Host::Human::getInCohort ) );
                }
            }
        }
    }
    if( intervElt.getImportedInfections().present() ){
        const scnXml::ImportedInfections& ii = intervElt.getImportedInfections().get();
        importedInfections.init( ii );
    }
    if( intervElt.getImmuneSuppression().present() ){
        const scnXml::ImmuneSuppression& elt = intervElt.getImmuneSuppression().get();
        if( elt.getTimed().present() ){
            // timed deployments:
            const scnXml::MassList::DeploySequence& seq = elt.getTimed().get().getDeploy();
            typedef scnXml::MassList::DeploySequence::const_iterator It;
            for( It it = seq.begin(); it != seq.end(); ++it ){
                timed.push_back( new TimedMassDeployment( *it, &Host::Human::immuneSuppression ) );
            }
        }
    }
    // Must come after vaccines are initialised:
    if( intervElt.getInsertR_0Case().present() ){
        const scnXml::InsertR_0Case& elt = intervElt.getInsertR_0Case().get();
        if( elt.getTimedDeployment().size() > 0 ){
            Host::Vaccine::verifyEnabledForR_0();
            // timed deployments:
            typedef scnXml::InsertR_0Case::TimedDeploymentSequence::const_iterator It;
            for( It it = elt.getTimedDeployment().begin(); it != elt.getTimedDeployment().end(); ++it ){
                timed.push_back( new TimedR_0Deployment( TimeStep( it->getTime() ) ) );
            }
        }
    }
    if( intervElt.getUninfectVectors().present() ){
        const scnXml::UninfectVectors& elt = intervElt.getUninfectVectors().get();
        if( elt.getTimedDeployment().size() > 0 ){
            // timed deployments:
            typedef scnXml::UninfectVectors::TimedDeploymentSequence::const_iterator It;
            for( It it = elt.getTimedDeployment().begin(); it != elt.getTimedDeployment().end(); ++it ){
                timed.push_back( new TimedUninfectVectorsDeployment( TimeStep( it->getTime() ) ) );
            }
        }
    }
    if( intervElt.getVectorPop().present() ){
        typedef scnXml::VectorPop::InterventionSequence SeqT;
        const SeqT& seq = intervElt.getVectorPop().get().getIntervention();
        size_t instance = 0;
        for( SeqT::const_iterator it = seq.begin(), end = seq.end(); it != end; ++it ){
            const scnXml::VectorIntervention& elt = *it;
            if (elt.getTimed().present() ) {
                population._transmissionModel->initVectorInterv( elt.getDescription().getAnopheles(), instance );
                
                const scnXml::TimedBaseList::DeploySequence& seq = elt.getTimed().get().getDeploy();
                typedef scnXml::TimedBaseList::DeploySequence::const_iterator It;
                for ( It it = seq.begin(); it != seq.end(); ++it ) {
                    timed.push_back( new TimedVectorDeployment(TimeStep( it->getTime() ), instance) );
                }
                instance++;
            }
        }
    }

    // lists must be sorted, increasing
    // For reproducability, we need to use stable_sort, not sort.
    // NOTE: I'd rather use stable_sort, but it's not available. Results are
    // the same without as with a hacked BOOST version including stable_sort.
    continuous.sort();
    timed.sort();
    
    // make sure the list ends with something always in the future, so we don't
    // have to check nextTimed is within range:
    timed.push_back( new DummyTimedDeployment() );
}

void InterventionManager::loadFromCheckpoint( OM::Population& population, TimeStep interventionTime ){
    // We need to re-deploy changeHS and changeEIR interventions, but nothing
    // else. nextTimed should be zero so we can go through all past interventions.
    // Only redeploy those which happened before this timestep.
    assert( nextTimed == 0 );
    while( timed[nextTimed].time < interventionTime ){
        if( dynamic_cast<TimedChangeHSDeployment*>(&timed[nextTimed])!=0 ||
            dynamic_cast<TimedChangeEIRDeployment*>(&timed[nextTimed])!=0 ){
            //Note: neither changeHS nor changeEIR interventions care what the
            //current timestep is when they are deployed, so we don't need to
            //tell them the deployment time.
            timed[nextTimed].deploy( population );
        }
        nextTimed += 1;
    }
}


void InterventionManager::deploy(OM::Population& population) {
    if( TimeStep::interventionPeriod < TimeStep(0) )
        return;
    
    // deploy imported infections (not strictly speaking an intervention)
    importedInfections.import( population );
    
    // deploy timed interventions
    while( timed[nextTimed].time <= TimeStep::interventionPeriod ){
        timed[nextTimed].deploy( population );
        nextTimed += 1;
    }
    
    // deploy continuous interventions
    for( Population::HumanIter it = population.getList().begin();
        it != population.getList().end(); ++it )
    {
        uint32_t nextCtsDist = it->getNextCtsDist();
        // deploy continuous interventions
        while( nextCtsDist < continuous.size() )
        {
            if( !continuous[nextCtsDist].filterAndDeploy( *it, population ) )
                break;  // deployment (and all remaining) happens in the future
            nextCtsDist = it->incrNextCtsDist();
        }
    }
}

}
