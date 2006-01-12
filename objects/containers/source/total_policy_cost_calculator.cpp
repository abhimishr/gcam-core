/*! 
* \file mac_generator_scenario_runner.cpp
* \ingroup Objects
* \brief TotalPolicyCostCalculator class source file.
* \author Josh Lurz
* \date $Date$
* \version $Revision$
*/

#include "util/base/include/definitions.h"
#include <cassert>
#include <vector>
#include <string>
#include "containers/include/scenario.h"
#include "containers/include/world.h"
#include "util/base/include/util.h"
#include "util/curves/include/curve.h"
#include "util/curves/include/point_set_curve.h"
#include "util/curves/include/point_set.h"
#include "util/curves/include/xy_data_point.h"
#include "util/base/include/configuration.h"
#include "util/base/include/model_time.h"
#include "marketplace/include/marketplace.h"
#include "util/base/include/xml_helper.h"
#include "util/curves/include/explicit_point_set.h"
#include "util/base/include/auto_file.h"
#include "util/logger/include/ilogger.h"
#include "containers/include/total_policy_cost_calculator.h"
#include "containers/include/iscenario_runner.h"
#include "emissions/include/ghg_policy.h"
#include "reporting/include/xml_db_outputter.h"

// Only compile this code if the XML database is turned on.
#if( __USE_XML_DB__ )
#include "dbxml/DbXml.hpp"
#endif

using namespace std;
using namespace xercesc;

// Only compile this code if the XML database is turned on.
#if( __USE_XML_DB__ )
using namespace DbXml;
#endif

/*! \brief Constructor.
* \param aSingleScenario The single scenario runner.
*/
TotalPolicyCostCalculator::TotalPolicyCostCalculator( IScenarioRunner* aSingleScenario ){
    assert( aSingleScenario );
    mSingleScenario = aSingleScenario;
    mGlobalCost = 0;
    mGlobalDiscountedCost = 0;
    mRanCosts = false;

    // Get the variables from the configuration.
    const Configuration* conf = Configuration::getInstance();
    mGHGName = conf->getString( "AbatedGasForCostCurves", "CO2" );
    mNumPoints = conf->getInt( "numPointsForCO2CostCurve", 5 );
}

//! Destructor. Deallocated memory for all the curves created. 
TotalPolicyCostCalculator::~TotalPolicyCostCalculator(){
    // This deletes all the curves.
    for( VectorRegionCurvesIterator outerDel = mEmissionsQCurves.begin(); outerDel != mEmissionsQCurves.end(); ++outerDel ){
        for( RegionCurvesIterator innerDel = outerDel->begin(); innerDel != outerDel->end(); ++innerDel ){
            delete innerDel->second;
        }
    }
    
    for( VectorRegionCurvesIterator outerDel = mEmissionsTCurves.begin(); outerDel != mEmissionsTCurves.end(); ++outerDel ){
        for( RegionCurvesIterator innerDel = outerDel->begin(); innerDel != outerDel->end(); ++innerDel ){
            delete innerDel->second;
        }
    }
    
    for( VectorRegionCurvesIterator outerDel = mPeriodCostCurves.begin(); outerDel != mPeriodCostCurves.end(); ++outerDel ){
        for( RegionCurvesIterator innerDel = outerDel->begin(); innerDel != outerDel->end(); ++innerDel ){
            delete innerDel->second;
        }
    }

    for( RegionCurvesIterator del = mRegionalCostCurves.begin(); del != mRegionalCostCurves.end(); ++del ){
        delete del->second;
    }
}

/*! \brief Function to create a cost curve for the mitigation policy.
* \details This function performs multiple calls to scenario.run() with 
* varied fixed carbon taxes in order to determine an abatement cost curve.
* \return Whether all model runs solved successfully.
* \author Josh Lurz
* \todo Find a better way to check for the existance of a carbon market taking into account 
* different carbon policies in different regions. 
*/
bool TotalPolicyCostCalculator::calculateAbatementCostCurve() {
    // If there is no policy market, the model will not create cost curves and 
    // will leave mRanCosts as false. This will prevent the cost curves from printing.
    if( mSingleScenario->getInternalScenario()->getMarketplace()->getPrice( mGHGName, "USA", 1 ) == Marketplace::NO_MARKET_PRICE ){
        ILogger& mainLog = ILogger::getLogger( "main_log" );
        mainLog.setLevel( ILogger::NOTICE );
        mainLog << "Skipping cost curve calculations for non-policy model run." << endl;
        return true;
    }

    // Set the size of the emissions curve vectors to the number of trials plus 1 for the base.
    mEmissionsQCurves.resize( mNumPoints + 1 );
    mEmissionsTCurves.resize( mNumPoints + 1 );

    // Get prices and emissions for the primary scenario run.
    mEmissionsQCurves[ mNumPoints ] = mSingleScenario->getInternalScenario()->getEmissionsQuantityCurves( mGHGName );
    mEmissionsTCurves[ mNumPoints ] = mSingleScenario->getInternalScenario()->getEmissionsPriceCurves( mGHGName );
    
    // Run the trials and store the cost curves.
    bool success = runTrials();
    
    // Create a cost curve for each period and region.
    createCostCurvesByPeriod();

    // Create a cost curve for each region and find regional and global costs.
    createRegionalCostCurves();

    // Return whether all trials completed successfully.
    mRanCosts = true;
    return success;
}

/*! \brief Run a trial for each point and store the abatement curves.
* \detailed First calculates a fraction of the total carbon tax to use, based 
* on the trial number and the total number of points, so that the datapoints are equally
* distributed from 0 to the full carbon tax for each period. It then calculates and 
* sets the fixed tax for each year. The scenario is then run, and the emissions and 
* tax curves are stored for each region.
* \return Whether all model runs completed successfully.
* \author Josh Lurz
*/
bool TotalPolicyCostCalculator::runTrials(){
    // Get the number of max periods.
    const Modeltime* modeltime = mSingleScenario->getInternalScenario()->getModeltime();
    const int maxPeriod = modeltime->getmaxper();

    bool success = true;
    // Loop through for each point.
    for( unsigned int currPoint = 0; currPoint < mNumPoints; currPoint++ ){
        // Determine the fraction of the full tax this tax will be.
        const double fraction = static_cast<double>( currPoint ) / static_cast<double>( mNumPoints );
        // Iterate through the regions to set different taxes for each if neccessary.
        // Currently this will set the same for all of them.
        for( CRegionCurvesIterator rIter = mEmissionsTCurves[ mNumPoints ].begin(); rIter != mEmissionsTCurves[ mNumPoints ].end(); ++rIter ){
            // Vector which will contain taxes for this trial.
            vector<double> currTaxes( maxPeriod );

            // Set the tax for each year. 
            for( int per = 0; per < maxPeriod; per++ ){
                const int year = modeltime->getper_to_yr( per );
                currTaxes[ per ] = rIter->second->getY( year ) * fraction;
            }
            // Set the fixed taxes into the world.
            auto_ptr<GHGPolicy> tax( new GHGPolicy( mGHGName, rIter->first, currTaxes ) );
            mSingleScenario->getInternalScenario()->setTax( tax.get() );
        }

        // Create an ending for the output files using the run number.
        ILogger& mainLog = ILogger::getLogger( "main_log" );
        mainLog.setLevel( ILogger::NOTICE );
        mainLog << "Starting cost curve point run number " << currPoint << "." << endl;

        // Run the scenario with the add-on extension to the output file names
        // as the point number. This allows the output file to be named debug +
        // point number.
        success &= mSingleScenario->getInternalScenario()->run( Scenario::RUN_ALL_PERIODS, true,
                                                                util::toString( currPoint ) );

        // Save information.
        mEmissionsQCurves[ currPoint ] = mSingleScenario->getInternalScenario()->getEmissionsQuantityCurves( mGHGName );
        mEmissionsTCurves[ currPoint ] = mSingleScenario->getInternalScenario()->getEmissionsPriceCurves( mGHGName );
    }
    return success;
}

/*! \brief Create a cost curve for each period and region.
* \detailed Using the cost curves generated by the trials, generate and stored a set of cost
* curves by period and region.
* \author Josh Lurz
*/
void TotalPolicyCostCalculator::createCostCurvesByPeriod() {
    // Create curves for each period based on all trials.
    const Modeltime* modeltime = mSingleScenario->getInternalScenario()->getModeltime();
    const int maxPeriod = mSingleScenario->getInternalScenario()->getModeltime()->getmaxper();
    mPeriodCostCurves.resize( maxPeriod );
    
    for( int per = 0; per < maxPeriod; per++ ){
        const int year = modeltime->getper_to_yr( per );
        // Iterate over each region.
        for( CRegionCurvesIterator rIter = mEmissionsQCurves[ 0 ].begin(); rIter != mEmissionsQCurves[ 0 ].end(); rIter++ ){
            ExplicitPointSet* currPoints = new ExplicitPointSet();
            const string region = rIter->first;
            // Iterate over each trial.
            for( unsigned int trial = 0; trial < mNumPoints + 1; trial++ ){
                double reduction = rIter->second->getY( year )
                                   - mEmissionsQCurves[ trial ][ region ]->getY( year );
                const double tax = mEmissionsTCurves[ trial ][ region ]->getY( year );
                XYDataPoint* currPoint = new XYDataPoint( reduction, tax );
                currPoints->addPoint( currPoint );
            }
            Curve* perCostCurve = new PointSetCurve( currPoints );
            perCostCurve->setTitle( region + " period cost curve" );
            perCostCurve->setNumericalLabel( per );
            mPeriodCostCurves[ per ][ region ] = perCostCurve;
        }
    }
}

/*! \brief Calculate final regional cost curves and total costs.
* \detailed Calculate for each region a final cost curve by integrating each period 
* cost curve from 0 to the total reduction in the initial constrain scenario. These are then
* used as datapoints to create a total cost curve for each region by period. These regional
* cost curves are then integrated and discounted based on a read-in discount rate. These values
* are both stored by region. A global sum for discounted and undiscounted values is stored as well.
* \author Josh Lurz
*/
void TotalPolicyCostCalculator::createRegionalCostCurves() {
    // Iterate through the regions again to determine the cost per period.
    const Configuration* conf = Configuration::getInstance();
    const double discountRate = conf->getDouble( "discountRate", 0.05 );
    const int startYear = conf->getInt( "discount-start-year", 2005 );

    const Modeltime* modeltime = mSingleScenario->getInternalScenario()->getModeltime();
    const int maxPeriod = modeltime->getmaxper();
    
    for( map<const string, const Curve*>::const_iterator rNameIter = mPeriodCostCurves[ 0 ].begin(); rNameIter != mPeriodCostCurves[ 0 ].end(); ++rNameIter ){
        // Skip the global curve which is only calculated for reporting.
        if( rNameIter->first == "global" ){
            continue;
        }
        ExplicitPointSet* costPoints = new ExplicitPointSet();

        // Loop through the periods. 
        for( int per = 0; per < maxPeriod; per++ ){
            const int year = modeltime->getper_to_yr( per );
            double periodCost = mPeriodCostCurves[ per ][ rNameIter->first ]->getIntegral( 0, DBL_MAX ); // Integrate from zero to the reduction.
            XYDataPoint* currPoint = new XYDataPoint( year, periodCost );
            costPoints->addPoint( currPoint );
        }
        Curve* regCostCurve = new PointSetCurve( costPoints );
        regCostCurve->setTitle( rNameIter->first );

        const double regionalCost = regCostCurve->getIntegral( startYear, modeltime->getEndYear() );

        // Temporary hardcoding of start year.
        const double discountedRegionalCost = regCostCurve->getDiscountedValue( startYear, modeltime->getEndYear(), discountRate );
        mRegionalCostCurves[ rNameIter->first ] = regCostCurve;
        mRegionalCosts[ rNameIter->first ] = regionalCost;
        mRegionalDiscountedCosts[ rNameIter->first ] = discountedRegionalCost;
    
        mGlobalCost += regionalCost;
        mGlobalDiscountedCost += discountedRegionalCost;
    }
}

/*! \brief Print the output.
* \details Print the output to an XML file, the Access database, and the XML
*          database.
*/
void TotalPolicyCostCalculator::printOutput() const {
    // Don't try to print output if the scenarios weren't run.
    if( !mRanCosts ){
        return;
    }
    
    // Create a string with the XML output.
    const string xmlString = createXMLOutputString();
    
    {
        // Open the XML output file and write to it.
        AutoOutputFile ccOut( "costCurvesOutputFileName",
                              "cost_curves_" + mSingleScenario->getInternalScenario()->getName() + ".xml" );
        ccOut << xmlString;
    }
    
    // Location to insert the information into the container.
    const string UPDATE_LOCATION = "/scenario/world/region[last()]";
    
    // Append the data to the XML database.
#if __USE_XML_DB__
    XMLDBOutputter::appendData( xmlString, UPDATE_LOCATION );
#endif

    // Write to the database.
    writeToDB();
}

/*! \brief Write total cost output to the Access database.
*/
void TotalPolicyCostCalculator::writeToDB() const {
    // Database function definition. 
    void dboutput4(string var1name,string var2name,string var3name,string var4name,
        string uname,vector<double> dout);

    const Modeltime* modeltime = mSingleScenario->getInternalScenario()->getModeltime();
    const int maxPeriod = modeltime->getmaxper();
    const double CVRT_75_TO_90 = 2.212; //  convert '75 price to '90 price
    vector<double> tempOutVec( maxPeriod );
    for( CRegionCurvesIterator rIter = mRegionalCostCurves.begin(); rIter != mRegionalCostCurves.end(); ++rIter ){
        // Write out to the database.
        for( int per = 0; per < maxPeriod; ++per ){
            tempOutVec[ per ] = rIter->second->getY( modeltime->getper_to_yr( per ) ) * CVRT_75_TO_90;
        }
        dboutput4(rIter->first,"General","PolicyCostUndisc","Period","(millions)90US$",tempOutVec);
    }

    // Write out undiscounted costs by region.
    tempOutVec.clear();
    tempOutVec.resize( maxPeriod );
    for( CRegionalCostsIterator iter = mRegionalCosts.begin(); iter != mRegionalCosts.end(); iter++ ){
        // regional total cost of policy
        tempOutVec[maxPeriod-1] = iter->second * CVRT_75_TO_90;
        dboutput4(iter->first,"General","PolicyCostTotalUndisc","AllYears","(millions)90US$",tempOutVec);
    }

    // Write out discounted costs by region.
    tempOutVec.clear();
    tempOutVec.resize( maxPeriod );
    typedef map<const string,double>::const_iterator constDoubleMapIter;
    for( constDoubleMapIter iter = mRegionalDiscountedCosts.begin(); iter != mRegionalDiscountedCosts.end(); iter++ ){
        // regional total cost of policy
        tempOutVec[maxPeriod-1] = iter->second * CVRT_75_TO_90;
        dboutput4(iter->first,"General","PolicyCostTotalDisc","AllYears","(millions)90US$",tempOutVec);
    }
}

/*! Create a string containing the XML output.
* \return A string containing the XML output.
*/
const string TotalPolicyCostCalculator::createXMLOutputString() const {
    // Create a buffer to contain the output.
    stringstream buffer;
    Tabs tabs;

    // Create a root tag.
    XMLWriteOpeningTag( "CostCurvesInfo", buffer, &tabs ); 

    XMLWriteOpeningTag( "PeriodCostCurves", buffer, &tabs );
    const double CVRT_75_TO_90 = 2.212; //  convert '75 price to '90 price
    const Modeltime* modeltime = mSingleScenario->getInternalScenario()->getModeltime();

    for( int per = 0; per < modeltime->getmaxper(); per++ ){
        const int year = modeltime->getper_to_yr( per );
        XMLWriteOpeningTag( "CostCurves", buffer, &tabs, "", year );
        for( CRegionCurvesIterator rIter = mPeriodCostCurves[ per ].begin(); rIter != mPeriodCostCurves[ per ].end(); rIter++ ){
            rIter->second->toInputXML( buffer, &tabs );
        }
        XMLWriteClosingTag( "CostCurves", buffer, &tabs );
    }
    XMLWriteClosingTag( "PeriodCostCurves", buffer, &tabs );
    
    XMLWriteOpeningTag( "RegionalCostCurvesByPeriod", buffer, &tabs );
    for( CRegionCurvesIterator rIter = mRegionalCostCurves.begin(); rIter != mRegionalCostCurves.end(); ++rIter ){
        rIter->second->toInputXML( buffer, &tabs );
    }
    XMLWriteClosingTag( "RegionalCostCurvesByPeriod", buffer, &tabs ); 
    
    XMLWriteOpeningTag( "RegionalUndiscountedCosts", buffer, &tabs );
    // Write out undiscounted costs by region.
    for( CRegionalCostsIterator iter = mRegionalCosts.begin(); iter != mRegionalCosts.end(); iter++ ){
        XMLWriteElement( iter->second, "UndiscountedCost", buffer, &tabs, 0, iter->first );
    }
    XMLWriteClosingTag( "RegionalUndiscountedCosts", buffer, &tabs );
     
    // Write out discounted costs by region.
    XMLWriteOpeningTag( "RegionalDiscountedCosts", buffer, &tabs );
    typedef map<const string,double>::const_iterator constDoubleMapIter;
    for( constDoubleMapIter iter = mRegionalDiscountedCosts.begin(); iter != mRegionalDiscountedCosts.end(); iter++ ){
        XMLWriteElement( iter->second, "DiscountedCost", buffer, &tabs, 0, iter->first );
    }
    XMLWriteClosingTag( "RegionalDiscountedCosts", buffer, &tabs );

    // Write out the total cost and discounted cost.
    XMLWriteElement( mGlobalCost, "GlobalUndiscountedTotalCost", buffer, &tabs );
    XMLWriteElement( mGlobalDiscountedCost, "GlobalDiscountedCost", buffer, &tabs );

    XMLWriteClosingTag( "CostCurvesInfo", buffer, &tabs );
    return buffer.str();
}