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
* \file input_driver.cpp
* \ingroup Objects
* \brief InputDriver source file.
* \author Jim Naslund
*/

#include "util/base/include/definitions.h"

#include <cassert>
#include <xercesc/dom/DOMNode.hpp>
#include <xercesc/dom/DOMNodeList.hpp>

#include "emissions/include/input_driver.h"
#include "functions/include/iinput.h"
#include "functions/include/function_utils.h"
#include "util/base/include/xml_helper.h"

using namespace std;
using namespace xercesc;

double InputDriver::calcEmissionsDriver( const vector<IInput*>& aInputs,
                                         const vector<IOutput*>& aOutputs,
                                         const int aPeriod ) const
{
    IInput* inputToDrive = FunctionUtils::getInput( aInputs, mInputName );
    return inputToDrive ? inputToDrive->getPhysicalDemand( aPeriod ) : 0.0;
}

InputDriver* InputDriver::clone() const {
    return new InputDriver( *this );
}

const string& InputDriver::getXMLName() const {
    return getXMLNameStatic();
}

const string& InputDriver::getXMLNameStatic(){
    static const string XML_NAME = "input-driver";
    return XML_NAME;
}

bool InputDriver::XMLParse( const xercesc::DOMNode* aNode ) {
    /*! \pre Assume we are passed a valid node. */
    assert( aNode );

    DOMNodeList* nodeList = aNode->getChildNodes();
    
    bool parsingSuccessful = true;

    for( unsigned int i = 0; i < nodeList->getLength(); ++i ) {
        DOMNode* curr = nodeList->item( i );
        string nodeName = XMLHelper<string>::safeTranscode( curr->getNodeName() );

        if( nodeName == XMLHelper<void>::text() ){
            continue;
        }
        else if( nodeName == "input-name" ){
            mInputName = XMLHelper<string>::getValue( curr );
        }
        else {
            ILogger& mainLog = ILogger::getLogger( "main_log" );
            mainLog.setLevel( ILogger::WARNING );
            mainLog << "Unrecognized text string: " << nodeName << " found while parsing" << getXMLName() << endl;
            parsingSuccessful = false;
        }
    }
    
    return parsingSuccessful;
}
