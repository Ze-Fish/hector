#ifndef TSERIES_H
#define TSERIES_H
/*
 *  tseries.h - the basic time series data type
 *  hector
 *
 *  Created by Ben on 9/14/10.
 *
 */

#include <map>
#include <limits>

#include "core/logger.hpp"
#include "data/h_interpolator.hpp"
#include "data/unitval.hpp"
#include "h_exception.hpp"

namespace Hector {
  
/*! \brief Time series data type.
 *
 *  Currently implemented as an STL map.
 */
template <class T_data>
class tseries {
    std::map<double, T_data> mapdata;
    double lastInterpYear;
	bool endinterp_allowed;
    mutable bool dirty;                 // does series need re-interpolating?
    
    h_interpolator interpolator;
    void set_interp( double, bool, interpolation_methods );
    void fit_spline();
    
public:
    tseries();

    void set( double, T_data );
    T_data get( double ) const throw( h_exception );
    bool exists( double ) const;
    
    double first() const;
    double last() const;
    
    int size() const;
    
    void allowInterp( bool eia );
    void allowPartialInterp( bool eia );
    
    std::string name;
};


//-----------------------------------------------------------------------
/*! \brief A helper class to allow template specialization for interpolation.
 *
 * The interpolator expects types to be double convertible and will not compile
 * if the templated type can not be implicitly converted.  Creating a specialization
 * of this class to explicitly convert to double will allow the use of tseries with
 * that type and run as expected including interpolation.
 *
 * \todo Should we create a specialization for non-double types that just throws
 *       an exception?  This will allow us to use tseries with that type just
 *       without interpolation.
 */
template<class T_data>
struct interp_helper {
    // TODO: we might want to consider re-organizing this to not have to pass
    // info around, discuss with Ben
    static T_data interp( const std::map<double, T_data>& userData, 
                          h_interpolator& interpolator, std::string name,
                          bool& isDirty, bool endinterp_allowed,
                          const double index ) throw( h_exception )
    {
        H_ASSERT( userData.size() > 1, "time series data must have size>1" );
        
        if( isDirty ) {       // data have changed; inform interpolator
 /*           Logger& glog = Logger::getGlobalLogger();
            H_LOG( glog, Logger::DEBUG) <<  
                "Informing interpolator of new data (" << name << ")" << std::endl; */
            double *x = new double[ userData.size() ];   // allocate
            double *y = new double[ userData.size() ];
            
            typename std::map<double,T_data>::const_iterator itr;    // ...and fill
            int i=0;
            for ( itr=userData.begin(); itr != userData.end(); itr++ ) {
                x[ i ] = (*itr).first;
                y[ i ] = (*itr).second;
                i++;
            }
            
            interpolator.newdata( i, x, y );
            
            delete [] x;           // spline will keep its own copy
            delete [] y;           // spline will keep its own copy
            isDirty = false;
        }
        
        if( index < (*userData.begin()).first || index > (*userData.rbegin()).first )       // beyond-end interpolation
            H_ASSERT( endinterp_allowed, "end interpolation not allowed" );
        
        return interpolator.f( index );
    }
};


//-----------------------------------------------------------------------
/*! \brief Interpolation specialization for boost units quantities.
 *
 * This will allow interpolation of any boost units quantity assuming that the
 * value type is convertible to double.  This is functionally equivalent to the
 * generic version however the double value is accessed via the .value() method
 * and the return value will be in the same units as the other quantities.
 */
template<>
struct interp_helper<unitval> {
    typedef unitval T_unit_type;
    // TODO: we might want to consider re-organizing this to not have to pass
    // info around, discuss with Ben
    static T_unit_type interp( const std::map<double, T_unit_type>& userData,
                              h_interpolator& interpolator, std::string name,
                              bool& isDirty, bool endinterp_allowed,
                              const double index ) throw( h_exception )
    {
        H_ASSERT( userData.size() > 1, "time series data must have size>1" );
        
        if( isDirty ) {       // data have changed; inform interpolator
 //           Logger& glog = Logger::getGlobalLogger();
 //           H_LOG( glog, Logger::DEBUG) << "Informing interpolator of new data (" << name << ")" << std::endl;
            double *x = new double[ userData.size() ];   // allocate
            double *y = new double[ userData.size() ];
            
            std::map<double,T_unit_type>::const_iterator itr;    // ...and fill
            int i=0;
            for ( itr=userData.begin(); itr != userData.end(); itr++ ) {
                x[ i ] = (*itr).first;
                y[ i ] = (*itr).second.value( (*itr).second.units() );
                i++;
            }
            
            interpolator.newdata( i, x, y );
            
            delete[] x;         // spline will keep its own copy
            delete[] y;         // spline will keep its own copy
            isDirty = false;
        }
        
        if( index < (*userData.begin()).first || index > (*userData.rbegin()).first )       // beyond-end interpolation
            H_ASSERT( endinterp_allowed, "end interpolation not allowed" );
        
        return unitval( interpolator.f( index ), (*(userData.begin())).second.units() );
    }
};


/*  "Because templates are compiled when required, this forces a restriction
    for multi-file projects: the implementation (definition) of a template 
    class or function must be in the same file as its declaration. That 
    means that we cannot separate the interface in a separate header file, 
    and that we must include both interface and implementation in any file 
    that uses the templates."   http://www.cplusplus.com/doc/tutorial/templates/
*/

//-----------------------------------------------------------------------
/*! \brief Constructor for time series data type.
 *
 *  Initializes internal variables.
 */
template <class T_data>
tseries<T_data>::tseries( ) {
/*
    Logger& glog = Logger::getGlobalLogger();
    H_LOG( glog, Logger::DEBUG) << "Creating time series" << std::endl; */
    set_interp( std::numeric_limits<double>::min(), false, DEFAULT );         // default values
    dirty = false;
    name = "?";
}

//-----------------------------------------------------------------------
/*! \brief 'Set' for time series data type.
 *
 *  Sets an (t, d) tuple, data d at time t.
 */
template <class T_data>
void tseries<T_data>::set( double t, T_data d ) {
    mapdata[ t ] = d;
    if( t < lastInterpYear ) {
        dirty = true;
    }
}

//-----------------------------------------------------------------------
/*! \brief Does data exist at time (position) t?
 *
 *  Returns a bool to indicate if data exists.
 */
template <class T_data>
bool tseries<T_data>::exists( double t ) const {
//    H_ASSERT( !mapdata.empty() );
    return ( mapdata.find( t ) != mapdata.end() );
}

//-----------------------------------------------------------------------
/*! \brief 'Get' for time series data type.
 *
 *  Returns data associated with time t.
 *  If no value exists, behavior is defined by 'interp_allowed'.
 */
template <class T_data>
T_data tseries<T_data>::get( double t ) const throw( h_exception ) {
    typename std::map<double,T_data>::const_iterator itr = mapdata.find( t );
    if( itr != mapdata.end() )
        return (*itr).second;
    else if( t < lastInterpYear )
        return interp_helper<T_data>::interp( mapdata, 
                                              const_cast<tseries*>( this )->interpolator,
                                              name, dirty, endinterp_allowed, t );
	else {
        Logger& glog = Logger::getGlobalLogger();
        H_LOG( glog, Logger::WARNING) << "Interpolation requested but not allowed (" << name << ") date: " << t << std::endl;

		H_THROW( "Interpolation requested but not allowed" );
    }
}

//-----------------------------------------------------------------------
/*! \brief Set interpolation policies for data.
 *
 *  Two parameters: Max date for which interpolation is allowed. And is end-
 *  interpolation allowed?
 */
template <class T_data>
void tseries<T_data>::set_interp( double ia, bool eia, interpolation_methods m ) {
    lastInterpYear = ia;
	endinterp_allowed = eia;
    interpolator.set_method( m );
    dirty = true;
}

//-----------------------------------------------------------------------
/*! \brief Set interpolation policies to allow interpolation at any date.
 *
 * \param eia Whether to allow extrapolation.
 */
template<class T_data>
void tseries<T_data>::allowInterp( bool eia ) {
    set_interp( std::numeric_limits<double>::max(), eia, DEFAULT );
}

//-----------------------------------------------------------------------
/*! \brief Only allow interpolation for dates less that the current max date.
 *
 * \param eia Whether to allow extrapolation.  This would only apply for the
 *            low-end range.
 */
template<class T_data>
void tseries<T_data>::allowPartialInterp( bool eia ) {
    set_interp( last(), eia, DEFAULT );
}

//-----------------------------------------------------------------------
/*! \brief Return index of first element in series.
 *
 *  Return index of first element in series.
 */
template <class T_data>
double tseries<T_data>::first() const {
    H_ASSERT( !mapdata.empty(), "no mapdata" );
    return (*mapdata.begin()).first;
}

//-----------------------------------------------------------------------
/*! \brief Return index of last element in series.
 *
 *  Return index of last element in series.
 */
template <class T_data>
double tseries<T_data>::last() const {
    H_ASSERT( !mapdata.empty(), "no mapdata" );
    return (*mapdata.rbegin()).first;
}

//-----------------------------------------------------------------------
/*! \brief Return size of series.
 *
 *  Return size of series.
 */
template <class T_data>
int tseries<T_data>::size() const {
    return int( mapdata.size() );
}

}

#endif
