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

#include "Pathogenesis/Mueller.h"
#include "Parameters.h"

#include <cmath>
using namespace std;

namespace OM { namespace Pathogenesis {
    
double MuellerPathogenesis::rateMultiplier_31;
double MuellerPathogenesis::densityExponent_32;

void MuellerPathogenesis::init( const Parameters& parameters ){
  rateMultiplier_31=parameters[Parameters::MUELLER_RATE_MULTIPLIER];
  densityExponent_32=parameters[Parameters::MUELLER_DENSITY_EXPONENT];
}

double MuellerPathogenesis::getPEpisode(double, double totalDensity) {
  double incidenceDensity = rateMultiplier_31 * (pow(totalDensity, densityExponent_32)) * TimeStep::yearsPerInterval;
  return 1.0-exp(-incidenceDensity);
}

} }