#ifndef _AEMISSONS_DRIVER_H_
#define _AEMISSONS_DRIVER_H_
#if defined(_MSC_VER)
#pragma once
#endif

/*
* LEGAL NOTICE
* This computer software was prepared by Battelle Memorial Institute,
* hereinafter the Contractor, under Contract No. DE-AC05-76RL0 1830
* with the Department of Energy (DOE). NEITHER THE GOVERNMENT NOR THE
* CONTRACTOR MAKES ANY WARRANTY, EXPRESS OR IMPLIED, OR ASSUMES ANY
* LIABILITY FOR THE USE OF THIS SOFTWARE. This notice including this
* sentence must appear on any copies of this computer software.
* 
* EXPORT CONTROL
* User agrees that the Software will not be shipped, transferred or
* exported into any country or used in any manner prohibited by the
* United States Export Administration Act or any other applicable
* export laws, restrictions or regulations (collectively the "Export Laws").
* Export of the Software may require some form of license or other
* authority from the U.S. Government, and failure to obtain such
* export control license may result in criminal liability under
* U.S. laws. In addition, if the Software is identified as export controlled
* items under the Export Laws, User represents and warrants that User
* is not a citizen, or otherwise located within, an embargoed nation
* (including without limitation Iran, Syria, Sudan, Cuba, and North Korea)
*     and that User is not otherwise prohibited
* under the Export Laws from receiving the Software.
* 
* Copyright 2011 Battelle Memorial Institute.  All Rights Reserved.
* Distributed as open-source under the terms of the Educational Community 
* License version 2.0 (ECL 2.0). http://www.opensource.org/licenses/ecl2.php
* 
* For further details, see: http://www.globalchange.umd.edu/models/gcam/
*
*/


/*!
 * \file aemissions_driver.h
 * \ingroup Objects
 * \brief AEmissionsDriver header file.
 * \author Jim Naslund
 */

#include <xercesc/dom/DOMNode.hpp>
#include <vector>
#include <string>

// Forward declaration
class IInput;
class IOutput;

/*! 
 * \ingroup Objects
 * \brief An abstract emissions driver class.
 * \details The class defines the behavior of an emissions driver.
 *          The class has one method and no members.
 * \author Jim Naslund
 */
class AEmissionsDriver{

public:
    /* \brief Returns an appropriate emissions driver.
     * \return A double representing an appropriate emissions driver.
     */
    virtual double calcEmissionsDriver( const std::vector<IInput*>& aInputs,
                                        const std::vector<IOutput*>& aOutputs,
                                        const int aPeriod ) const = 0;
    //! Clone operator.
    virtual AEmissionsDriver* clone() const = 0;
    /*
     * \brief Static method to get a string representing the type of driver.
     * \return A string representing the type of driver.
     */
    virtual const std::string& getXMLName() const = 0;
    
    //! XML parse
    virtual bool XMLParse( const xercesc::DOMNode* aNode ) = 0;
};


#endif // _AEMISSONS_DRIVER_H_
