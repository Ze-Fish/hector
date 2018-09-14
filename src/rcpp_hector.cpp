#include <Rcpp.h>
#include <fstream>
#include <sstream>

#include "hector.hpp"
#include "logger.hpp"
#include "message_data.hpp"

using namespace Rcpp;

// [[Rcpp::plugins("cpp11")]]


/* Non exported helper functions
 * These are intended for use by the C++ wrappers and are not callable from R directly.
 */

// Get a pointer to a core from the R handle. 
Hector::Core *gethcore(List core)
{
    int idx = core[0];
    Hector::Core *hcore = Hector::Core::getcore(idx);
    if(!hcore) {
        Rcpp::stop("hector::run:  invalid index");
    }
    return hcore;
}


//' Create and initialize a new hector instance
//'
//' The object returned is a handle to the newly created instance.  It will be required as an
//' argument for all functions that operate on the instance.  Creating multiple instances
//' simultaneously is supported.
//'
//' @param infile (String) name of the hector input file.
//' @param loglevel (int) minimum message level to output in logs (see \code{\link{loglevels}}).
//' @param suppresslogging (bool) If true, suppress all logging (loglevel is ignored in this case).
//' @return handle for the Hector instance.
//' @export
// [[Rcpp::export]]
List newcore(String inifile, int loglevel = 0, bool suppresslogging=false)
{
    try {
        // Check that the configuration file exists. The easiest way to do
        // this is to try to open it.
        std::ifstream ifs(inifile);      // we'll use this to test if the file exists
        if(ifs) {
            ifs.close();            // don't actually want to read from it
        }
        else {
            std::string fn = inifile;
            Rcpp::stop(std::string("Input file ") + fn + std::string(" does not exist."));
        }

        // Initialize the global logger
        Hector::Logger &glog = Hector::Logger::getGlobalLogger();
        glog.open("hector.log", false, !suppresslogging, (Hector::Logger::LogLevel) loglevel);
        H_LOG(glog, Hector::Logger::DEBUG) << "Hector logger initialized" << std::endl;
        

        // Create and initialize the core.
        int coreidx = Hector::Core::mkcore();
        Hector::Core *hcore = Hector::Core::getcore(coreidx);
        hcore->init();

        Rcout << "Core initialized\n";

        try {
            Hector::INIToCoreReader coreParser(hcore);
            coreParser.parse(inifile);
            Rcout << "Core parser run\n";
        }
        catch(h_exception e) {
            std::stringstream msg;
            msg << "While parsing hector input file: " << e;
            Rcpp::stop(msg.str());
        }


        // The Following three lines of code are occasionally useful for
        // generating debugging output; however, they leak memory.
        // Therefore, they should only be used for short tests where
        // you need the CSV output to compare to a benchmark run.
        // TODO:  Remove these before release.
        // std::ofstream *output = new std::ofstream("rcpp-test-output.csv");
        // Hector::CSVOutputStreamVisitor *csvosv = new Hector::CSVOutputStreamVisitor(*output);
        // hcore->addVisitor(csvosv);

        // Run the last bit of setup
        hcore->prepareToRun();

        // Construct the object we are going to return to R
        double strtdate = hcore->getStartDate();
        double enddate = hcore->getEndDate();

        List rv= List::create(coreidx, strtdate, enddate, inifile, true);
        rv.attr("class") = "hcore";
        return rv;
    }
    catch(h_exception e) {
        std::stringstream msg;
        msg << "During hector core setup: " << e;
        Rcpp::stop(msg.str());
    }
}


//' Shutdown a hector instance
//'
//' Shutting down an instance will free the instance itself and all of the objects it created. Any attempted
//' operation on the instance after that will raise an error.
//'
//' @section Caution:
//' This function should be called as \code{mycore <- shutdown(mycore)} so that the change
//' from active to inactive will be recorded in the caller.
//'
//' @param Handle to the Hector instance that is to be shut down.
//' @return The Hector handle, modified to show that it is no longer active.
//' @export
// [[Rcpp::export]]
List shutdown(List core)
{
    // TODO: check that the list supplied is an hcore object

    int idx = core[0];
    Hector::Core::delcore(idx);

    core[4] = false;

    return core;
}


//' Run the Hector climate model
//'
//' Run Hector up through the specified time.  This function does not return the results
//' of the run.  To get results, run \code{fetch}.
//'
//' @param core Handle a Hector instance that is to be run.
//' @param runtodate Date to run to.  The default is to run to the end date configured
//' in the input file used to initialize the core.
//' @export
// [[Rcpp::export]]
void run(List core, double runtodate=-1.0)
{
    Hector::Core *hcore = gethcore(core);
    try {
        hcore->run(runtodate);
    }
    catch(h_exception e) {
        std::stringstream msg;
        msg << "Error while running hector:  " << e;
        Rcpp::stop(msg.str());
    }
}


//' Get the current date for a Hector instance
//'
//' The "current date" is the last year that the Hector instance has completed.
//'
//' @param core Handle to a Hector instance
//' @return The current date in the Hector instance
//' @export
// [[Rcpp::export]]
double getdate(List core)
{
    Hector::Core *hcore = gethcore(core);
    return hcore->getCurrentDate();
}


//' Send a message to a Hector instance
//'
//' Messages are the mechanism used to get data from Hector model components and
//' to set values within components.
//' 
//' A message comprises a type (e.g. GETDATA to retrieve data from a component, or SETDATA to
//' set data in a component), a capability, which identifies the information to be operated
//' on (e.g. Atmospheric CO2 concentration, or global total radiative forcing), and an optional
//' structure of extra data (comprising a date and a numerical value with units).
//'
//' The arguments to this function are organized in a slightly more R-friendly way.  The message
//' type and capability are each passed as a single string.  The date portion of the extra
//' data is passed as a numeric vector (one message will be generated for each date).  The value
//' portion of the extra data is a numeric vector with a length of either 1 or the same length
//' as the date vector.  The units portion is a single string (we don't support sending a vector
//' of values with heterogeneous units in a single call.
//'
//' Either the date or the value (or both) may be NA.  The date should be NA in cases where the
//' parameter being referenced doesn't change with time.  The value should be NA in cases where
//' the optional data will be ignored.
//'
//' @param core a Hector instance
//' @param msgtype (String) type of message. Usually either GETDATA or SETDATA
//' @param capability (String) capability being targeted by the message.  The core will use
//' this information to look up the component to route the message to.
//' @param date (NumericVector) Date for which to set/get the variable.  Use NA if there is
//' no applicable date.
//' @param value (NumericVector) Numeric portion of the optional data (in case of setting
//' a value this will be the value to set).  The length of this vector should match that of
//' the time, or it should be length 1 (in which case it will be recycled).
//' @param unit (String) Unit for the value vector.
//' @export
// [[Rcpp::export]]
DataFrame sendmessage(List core, String msgtype, String capability, NumericVector date,
                      NumericVector value, String unit)
{
    Hector::Core *hcore = gethcore(core);
    
    if(value.size() != date.size() && value.size() != 1) {
        Rcpp::stop("Value must have length 1 or same length as date.");
    }

    int N = date.size();
    std::string msgstr = msgtype;
    std::string capstr = capability;

    // We need to convert the unit string into an enumerated type
    std::string unitstr = unit;
    Hector::unit_types utype;
    try {
        utype = Hector::unitval::parseUnitsName(unitstr);
    }
    catch(h_exception e) {
        // std::stringstream emsg;
        // emsg << "sendmessage: invalid unit type: " << unitstr;
        // Rcpp::stop(emsg.str());
        utype = Hector::U_UNDEFINED;
    }

    NumericVector valueout(N);
    StringVector unitsout(N);

    try {
        for(size_t i=0; i<N; ++i) {
            // Construct the inputs to sendmessage
            int ival;               // location of the value we're looking for
            if(value.size() == 1)
                ival = 0;
            else
                ival = i;
            
            double tempval;
            if(NumericVector::is_na(value[ival]))
                tempval = 0;
            else
                tempval = value[ival];
            
            double tempdate;
            if(NumericVector::is_na(date[i]))
                tempdate = Hector::Core::undefinedIndex();
            else
                tempdate = date[i];
            
            Hector::message_data info(tempdate, Hector::unitval(tempval, utype));
            
            Hector::unitval rtn = hcore->sendMessage(msgstr, capstr, info);
            
            unitsout[i] = rtn.unitsName();
            valueout[i] = rtn.value(rtn.units());
        }
    }
    catch(h_exception e) {
        std::stringstream emsg;
        emsg << "sendmessage: " << e;
        Rcpp::stop(emsg.str());
    }

    // Assemble a data frame with the results: date, var, value, units
    DataFrame result =
        DataFrame::create(Named("date")=date, Named("var")=capability,
                          Named("value")=valueout, Named("units")=unitsout);

    return result;
}
