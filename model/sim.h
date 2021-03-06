/* This file is part of OpenMalaria.
 * 
 * Copyright (C) 2005-2015 Swiss Tropical and Public Health Institute
 * Copyright (C) 2005-2015 Liverpool School Of Tropical Medicine
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

#ifndef Hmod_OM_SimTime
#define Hmod_OM_SimTime

#ifndef Hmod_Global
#error "Please include Global.h not SimTime.h directly."
// because checkpoint.h needs this, and we need checkpoint.h
#endif

#include "util/mod.h"
#include "util/checkpoint.h"
#include <memory>

class UnittestUtil;

namespace scnXml {
    class Scenario;
}

namespace OM {

class SimTime;

inline int floorToInt( double x ){
	return static_cast<int>(std::floor(x));
}

/// Internal use only
// Required to avoid use-before-definition
class SimData {
    enum { DAYS_IN_YEAR = 365 };
    
    static int interval;        // days per time step
    static size_t steps_per_year;
    static double years_per_step;
    
    friend class SimTime;
    friend class SimDate;
    friend class sim;
};


/******************************************************************************
 * Class encapsulating simulation durations and times relative to the start.
 * 
 * Time steps, days and dates are derived from this. The values and units of
 * internal variables are an implementation detail (i.e. code outside this
 * class should not need to know).
 * 
 * The simulation always starts at time zero. "Intervention time" is a separate
 * concept (see SimDate).
 * 
 * Granularity: 1 day.
 *****************************************************************************/
class SimTime {
    /** Construct, from a time in days. */
    explicit SimTime( int days ) : d(days) {}
    
public:
    ///@brief Unparameterised constructors
    //@{
    /** Default construction; same as SimTime::never(). */
    SimTime() : d(-0x3FFFFFFF) {}
    
    /** Duration zero and the time at the start of the simulation. */
    static inline SimTime zero(){ return SimTime(0); }
    
    /** Special value representing a time point always in the past, such that
     * never() + x < zero() and x - never() will not to overflow for all valid
     * simulation times x (including any value now() may take as well as
     * never() and future()). */
    static inline SimTime never(){ return SimTime(); }
    
    /** Special value representing a time point always in the future, such that
     * now() < future() and now() + future() does not overflow. */
    static inline SimTime future(){ return SimTime(0x3FFFFFFF); }
    
    /** One day. */
    static inline SimTime oneDay(){ return SimTime(1); }
    
    /** One year. See SimData::DAYS_IN_YEAR. */
    static inline SimTime oneYear(){ return SimTime(SimData::DAYS_IN_YEAR); }
    
    /** One time step (currently either one or five days). */
    static inline SimTime oneTS(){ return SimTime(SimData::interval); }
    //@}
    
    ///@brief Parameterised constructors
    //@{
    /** Convert. */
    static inline SimTime fromTS(int ts){ return oneTS() * ts; }
    
    /** Duration in days. Should be fast (currently no conversion required). */
    static inline SimTime fromDays(int days){ return SimTime(days); }
    
    /** Convert from a whole number of years. */
    static inline SimTime fromYearsI(int years){
        return SimTime(SimData::DAYS_IN_YEAR * years);
    }
    
    /** Convert from years to nearest time step. */
    static inline SimTime fromYearsN(double years){
        return roundToTSFromDays(SimData::DAYS_IN_YEAR * years);
    }
    
    /** Convert from years, rounding down to the next time step. */
    static inline SimTime fromYearsD(double years){
        return fromTS( floorToInt(SimData::steps_per_year * years) );
    }
    
    /** Round to the nearest time-step, where input is in days. */
    static inline SimTime roundToTSFromDays(double days){
        return fromTS( floorToInt( days / SimData::interval + 0.5 ));
    }
    //@}
    
    ///@brief Conversions to other types/units
    //NOTE: these methods provide good documentation of the types of things
    //one does with SimTimes (besides comparing with other SimTimes).
    //@{
    /// Get length of time in days. Currently this is simple no-op get.
    inline int inDays() const{ return d; }
    
    /// Convert to years
    inline double inYears() const{ return d * (1.0 / SimData::DAYS_IN_YEAR); }
    
    /// Convert to time steps (rounding down)
    inline int inSteps() const{ return d / SimData::interval; }
    //@}
    
    ///@brief Simple arithmatic modifiers (all return a copy)
    //@{
    inline SimTime operator-()const {
        return SimTime( -d );
    }
    inline SimTime operator-( const SimTime rhs )const {
        return SimTime( d - rhs.d );
    }
    inline SimTime operator+( const SimTime rhs )const {
        return SimTime( d + rhs.d );
    }
    // scale by an integer
    inline SimTime operator*( int scalar )const {
        return SimTime( d * scalar );
    }
    // scale by a double, rounding to nearest
    inline SimTime operator*( double scalar )const {
        return SimTime( static_cast<int>(d * scalar + 0.5) );
    }
    // Divide by another SimTime; result is unitless. Note integer division.
    inline int operator/( const SimTime rhs )const{
        return d / rhs.d;
    }
    //@}
    
    ///@brief Self-modifying arithmatic
    //@{
    inline void operator+=( const SimTime rhs ) {
        d += rhs.d;
    }
    //@}
    
    ///@brief Comparators between two SimTimes (all return a boolean)
    //@{
    inline bool operator==( const SimTime rhs )const {
        return  d == rhs.d;
    }
    inline bool operator!=( const SimTime rhs )const {
        return  d != rhs.d;
    }
    inline bool operator>( const SimTime rhs )const {
        return  d > rhs.d;
    }
    inline bool operator>=( const SimTime rhs )const {
        return  d >= rhs.d;
    }
    inline bool operator<( const SimTime rhs )const {
        return  d < rhs.d;
    }
    inline bool operator<=( const SimTime rhs )const {
        return  d <= rhs.d;
    }
    //@}
    
    /** Return this time in time steps modulo some positive integer. */
    inline int moduloSteps(int denominator){
        return util::mod_nn(d / SimData::interval, denominator);
    }
    
    /** Return this time in time steps modulo some positive integer. */
    inline int moduloYearSteps(){
        return util::mod_nn(d / SimData::interval, SimData::steps_per_year);
    }
    
    /// Checkpointing
    template<class S>
    void operator& (S& stream) {
        using namespace OM::util::checkpoint;
        d & stream;
    }
    
private:
    int d;      // time in days
    
    friend class SimDate;
    friend SimTime mod_nn( const SimTime lhs, const SimTime rhs );
    friend ostream& operator<<( ostream& stream, SimTime date );
};

/******************************************************************************
 * Class encapsulating simulation time, from the point-of-view of interventions.
 * 
 * Intervention times may be specified as dates or as a delay since the start
 * of the intervention period.
 *****************************************************************************/
class SimDate {
    /** Construct, from a time in days. */
    explicit SimDate( int days ) : d(days) {}
    
public:
    ///@brief Constructors
    //@{
    /** Point zero of our date system: 0000-01-01. */
    static inline SimDate origin(){ return SimDate(0); }
    
    /** Default construction; same as SimDate::never(). */
    SimDate() : d(-0x3FFFFFFF) {}
    
    /** Special value representing a time point always in the past, such that
     * never() + x < zero() and x - never() will not to overflow for all valid
     * simulation times x (including any value now() may take as well as
     * never() and future()). */
    static inline SimDate never(){ return SimDate(); }
    
    /** Special value representing a time point always in the future, such that
     * now() < future() and now() + future() does not overflow. */
    static inline SimDate future(){ return SimDate(0x3FFFFFFF); }
    //@}
    
    ///@brief Simple arithmatic modifiers (all return a copy)
    //@{
    inline SimDate operator-( const SimTime rhs )const {
        return SimDate( d - rhs.d );
    }
    inline SimTime operator-( const SimDate rhs )const {
        return SimTime( d - rhs.d );
    }
    inline SimDate operator+( const SimTime rhs )const {
        return SimDate( d + rhs.d );
    }
    //@}
    
    ///@brief Self-modifying arithmatic
    //@{
    inline void operator+=( const SimTime rhs ) {
        d += rhs.d;
    }
    //@}
    
    ///@brief Comparators between two SimDates (all return a boolean)
    //@{
    inline bool operator==( const SimDate rhs )const {
        return  d == rhs.d;
    }
    inline bool operator!=( const SimDate rhs )const {
        return  d != rhs.d;
    }
    inline bool operator>( const SimDate rhs )const {
        return  d > rhs.d;
    }
    inline bool operator>=( const SimDate rhs )const {
        return  d >= rhs.d;
    }
    inline bool operator<( const SimDate rhs )const {
        return  d < rhs.d;
    }
    inline bool operator<=( const SimDate rhs )const {
        return  d <= rhs.d;
    }
    //@}
    
    /// Checkpointing
    template<class S>
    void operator& (S& stream) {
        using namespace OM::util::checkpoint;
        d & stream;
    }
    
private:
    int d;      // time in days
    
    friend ostream& operator<<( ostream& stream, SimDate date );
};

inline SimTime mod_nn( const SimTime lhs, const SimTime rhs ){
    return SimTime(util::mod_nn(lhs.d, rhs.d));
}

/** Encapsulates static variables: sim time. */
class sim {
public:
    ///@brief Simulation constants
    //@{
    /// Number of days in a year; defined as 365 (leap years are not simulated).
    enum { DAYS_IN_YEAR = 365 };
    
    /** The number of time steps in one year. */
    static inline size_t stepsPerYear(){ return SimData::steps_per_year; }
    
    /** A cached value: one year divided by one time step. */
    static inline double yearsPerStep(){ return SimData::years_per_step; }
    
    /// Maximum possible age of a human.
    static inline SimTime maxHumanAge(){ return s_max_human_age; }
    
    /// The starting date of the simulation
    static inline SimDate startDate() { return s_start; }
    
    /// The ending date of the simulation
    static inline SimDate endDate() { return s_end; }
    //@}
    
    ///@brief Access simulation time variables
    //@{
    /** Time at the beginning of a time step update.
     *
     * This is what is mostly used during an update. It is never negative and
     * increases throughout the simulation. */
    static inline SimTime ts0(){
        assert(in_update);      // should only be used during updates
        return s_t0;
    }
    /** Time at the end of a time step update.
     * 
     * During an update, ts0() + oneTS() = ts1(). Neither this nor ts0 should
     * be used outside of updates. */
    static inline SimTime ts1(){
        assert(in_update);      // should only be used during updates
        return s_t1;
    }
    /**
     * Time steps are mid-day to mid-day, and this is the time at mid-day (i.e.
     * this equals ts1 from the last step and ts0 from the next one).
     * 
     * This is for monitoring and intervention deployment which happens between
     * updates. Cannot be used during human or vector update.
     */
    static inline SimTime now(){
        assert(!in_update);     // only for use outside of step updates
        return s_t0;   // which is equal to s_t1 outside of updates, but that's a detail
    }
    /** During updates, this is ts0; between, this is now. */
    static inline SimTime nowOrTs0(){ return s_t0; }
    /** During updates, this is ts1; between, this is now. */
    static inline SimTime nowOrTs1(){ return s_t1; }
    /** During updates, this is ts0; between, it is now - 1. */
    static inline SimTime latestTs0(){ return s_t1 - SimTime::oneTS(); }
    //@}
    
    ///@brief Access intervention-time variables
    //@{
    /// Time relative to the start of the intervention period.
    /// 
    /// This equals (intervDate() - startDate()), but happens to be the most
    /// common way that intervention-period dates are used.
    static inline SimTime intervTime() {
        return s_interv;
    }
    
    /// The current date.
    /// 
    /// Only valid during the intervention phase, since the duration required
    /// for warm-up is not known in advance. (In prior phases, this function
    /// returns a large negative value.)
    /// 
    /// Intervention deployment times are relative to this date.
    static inline SimDate intervDate(){ return s_start + s_interv; }
    //@}
    
private:
    // Initial set-up: called by Simulator
    static void init( const scnXml::Scenario& scenario );
    
    // Start of update: called by Simulator
    static inline void start_update(){
        s_t1 += SimTime::oneTS();
#ifndef NDEBUG
        in_update = true;
#endif
    }
    // End of update: called by Simulator
    static inline void end_update(){
#ifndef NDEBUG
        in_update = false;
#endif
        s_t0 = s_t1;
        s_interv += SimTime::oneTS();
    }
    
    // Scenario constants
    static SimDate s_start;
    static SimDate s_end;
    
    static SimTime s_max_human_age;
    
    // Global variables
#ifndef NDEBUG
    static bool in_update;       // only true during human/population/transmission update
#endif
    static SimTime s_t0;
    static SimTime s_t1;
    
    static SimTime s_interv;
    
    friend class Simulator;
    friend class ::UnittestUtil;
};

}
#endif
