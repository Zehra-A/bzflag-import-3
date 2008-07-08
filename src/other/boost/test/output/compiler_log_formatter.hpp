//  (C) Copyright Gennadiy Rozental 2005.
//  Distributed under the Boost Software License, Version 1.0.
//  (See accompanying file LICENSE_1_0.txt or copy at 
//  http://www.boost.org/LICENSE_1_0.txt)

//  See http://www.boost.org/libs/test for the library home page.
//
//  File        : $RCSfile$
//
//  Version     : $Revision: 36026 $
//
//  Description : contains compiler like Log formatter definition
// ***************************************************************************

#ifndef BOOST_TEST_COMPILER_LOG_FORMATTER_HPP_020105GER
#define BOOST_TEST_COMPILER_LOG_FORMATTER_HPP_020105GER

// Boost.Test
#include <boost/test/detail/global_typedef.hpp>
#include <boost/test/unit_test_log_formatter.hpp>

#include <boost/test/detail/suppress_warnings.hpp>

//____________________________________________________________________________//

namespace boost {

namespace unit_test {

namespace output {

// ************************************************************************** //
// **************             compiler_log_formatter           ************** //
// ************************************************************************** //

class BOOST_TEST_DECL compiler_log_formatter : public unit_test_log_formatter {
public:
    // Formatter interface
    void    log_start( std::ostream&, counter_t test_cases_amount );
    void    log_finish( std::ostream& );
    void    log_build_info( std::ostream& );

    void    test_unit_start( std::ostream&, test_unit const& tu );
    void    test_unit_finish( std::ostream&, test_unit const& tu, unsigned long elapsed );
    void    test_unit_skipped( std::ostream&, test_unit const& tu );

    void    log_exception( std::ostream&, log_checkpoint_data const&, const_string explanation );

    void    log_entry_start( std::ostream&, log_entry_data const&, log_entry_types let );
    void    log_entry_value( std::ostream&, const_string value );
    void    log_entry_finish( std::ostream& );

protected:
    virtual void    print_prefix( std::ostream&, const_string file, std::size_t line );
};

} // namespace output

} // namespace unit_test

} // namespace boost

//____________________________________________________________________________//

#include <boost/test/detail/enable_warnings.hpp>

// ***************************************************************************
//  Revision History :
//  
//  $Log$
//  Revision 1.2.6.1  2006/11/13 20:06:57  jhunold
//  Merge from HEAD:
//  Added missing export declarations.
//
//  Revision 1.2  2005/02/20 08:27:08  rogeeff
//  This a major update for Boost.Test framework. See release docs for complete list of fixes/updates
//
//  Revision 1.1  2005/02/01 08:59:39  rogeeff
//  supplied_log_formatters split
//  change formatters interface to simplify result interface
//
// ***************************************************************************

#endif // BOOST_TEST_COMPILER_LOG_FORMATTER_HPP_020105GER
