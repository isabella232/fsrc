#define BOOST_TEST_MODULE Performance

#include "boost/algorithm/searching/boyer_moore_horspool.hpp"
#include "boost/algorithm/searching/knuth_morris_pratt.hpp"
#include "boost/test/unit_test.hpp"
#include "boost/timer/timer.hpp"
#include "boost/asio.hpp"
using boost::asio::post;

#include <fcntl.h>

#if !WIN32
#include <sys/mman.h>
#include <unistd.h>
#endif

#include "utils.hpp"
#include "threadpool.hpp"

utils::Lines parseContentForLoop( const char* data, const size_t size ) {
    utils::Lines lines;
    lines.reserve( 128 );

    if( size == 0 ) { return lines; }

    char* c_old = ( char* )data;
    char* c_new = c_old;
    char* c_end = c_old + size;

    for( ; *c_new; ++c_new ) {
        // just skip windows line endings
        if( *c_new == '\r' ) {
            ++c_new;
        }

        if( *c_new == '\n' ) {
            lines.emplace_back( c_old, c_new - c_old );
            c_old = c_new + 1;
        }
    }

    if( c_old != c_end ) {
        lines.emplace_back( c_old, c_end - c_old );
    }

    lines.shrink_to_fit();
    return lines;
}

utils::Lines parseContentFind( const char* data, const size_t size ) {
    utils::Lines lines;
    lines.reserve( 128 );

    if( size == 0 ) { return lines; }

    std::string_view view( data, size );
    size_t pos_old = 0;
    size_t pos_new = view.find( '\n' );

    while( pos_new != std::string::npos ) {
        lines.emplace_back( data + pos_old, pos_new - pos_old );
        pos_old = pos_new + 1;
        pos_new = view.find( '\n', pos_old );
    }

    if( pos_old != size ) {
        lines.emplace_back( data + pos_old, size - pos_old );
    }

    lines.shrink_to_fit();
    return lines;
}

using parseContentFunc = utils::Lines( const char* data, const size_t size );

//! POSIX API with custom parseContent function
utils::FileView fromFileParser( const sys_string& filename, parseContentFunc& parse ) {
    utils::FileView view;
    int file = open( filename.c_str(), O_RDONLY | O_BINARY );
    IF_RET( file == -1 );
    utils::ScopeGuard onExit( [file] { close( file ); } );

    view.size = utils::fileSize( file );
    IF_RET( !view.size );

    // growing buffer for each thread
    static thread_local utils::Buffer buffer;
    char* ptr = buffer.grow( view.size );

    size_t bytes = _read( file, ptr, view.size );
    IF_RET( view.size != bytes );

    // check first 100 bytes for binary
    IF_RET( !utils::isTextFile( std::string_view( ptr, std::min<size_t>( view.size, 100ul ) ) ) );

    view.lines = parse( ptr, view.size );
    return view;
}

//! POSIX API with thread local storage
utils::FileView fromFilePosix( const sys_string& filename ) {
    utils::FileView view;
    int file = open( filename.c_str(), O_RDONLY | O_BINARY );
    IF_RET( file == -1 );
    utils::ScopeGuard onExit( [file] { close( file ); } );

    view.size = utils::fileSize( file );
    IF_RET( !view.size );

    // growing buffer for each thread
    static thread_local utils::Buffer buffer;
    char* ptr = buffer.grow( view.size );

    size_t bytes = _read( file, ptr, view.size );
    IF_RET( view.size != bytes );

    // check first 100 bytes for binary
    IF_RET( !utils::isTextFile( std::string_view( ptr, std::min<size_t>( view.size, 100ul ) ) ) );

    view.lines = utils::parseContent( ptr, view.size );
    return view;
}

#if !WIN32
//! memory mapped API with thread local storage
utils::FileView fromFileMmap( const sys_string& filename ) {
    utils::FileView view;
    int file = open( filename.c_str(), O_RDONLY );
    IF_RET( file == -1 );
    utils::ScopeGuard onExit( [file] { close( file ); } );

    view.size = utils::fileSize( file );
    IF_RET( !view.size );

    char* map = ( char* )mmap( 0, view.size, PROT_READ, MAP_PRIVATE, file, 0 );
    IF_RET( map == MAP_FAILED );

    // check first 100 bytes for binary
    IF_RET( !utils::isTextFile( std::string_view( map, std::min<size_t>( view.size, 100ul ) ) ) );

    view.lines = utils::parseContent( map, view.size );
    munmap( map, view.size ); // if used, call munmap after parsing
    return view;
}
#endif

//! C API with thread local storage
utils::FileView fromFileLocal( const sys_string& filename ) {
    utils::FileView view;
    FILE* file = fopen( filename.c_str(), O_RB );
    IF_RET( file == NULL );
    const utils::ScopeGuard onExit( [file] { fclose( file ); } );

    view.size = utils::fileSize( fileno( file ) );
    IF_RET( !view.size );

    // growing buffer for each thread
    static thread_local utils::Buffer buffer;
    char* ptr = buffer.grow( view.size );

    // read content
    IF_RET( view.size != fread( ptr, 1, view.size, file ) );

    // check first 100 bytes for binary
    IF_RET( !utils::isTextFile( std::string_view( ptr, std::min<size_t>( view.size, 100ul ) ) ) );

    view.lines = utils::parseContent( ptr, view.size );
    return view;
}

//! C API with string storage
utils::FileView fromFileString( const sys_string& filename ) {
    utils::FileView view;
    FILE* file = fopen( filename.c_str(), O_RB );
    IF_RET( file == NULL );
    const utils::ScopeGuard onExit( [file] { fclose( file ); } );

    view.size = utils::fileSize( fileno( file ) );
    IF_RET( !view.size );

    std::string buffer;
    buffer.resize( view.size );
    const char* ptr = buffer.data();

    // read content
    IF_RET( view.size != fread( ( void* )ptr, 1, view.size, file ) );

    // check first 100 bytes for binary
    IF_RET( !utils::isTextFile( std::string_view( ptr, std::min<size_t>( view.size, 100ul ) ) ) );

    view.lines = utils::parseContent( ptr, view.size );
    // if used, add buffer to FileView
    return view;
}

//! CPP API with thread local storage
utils::FileView fromFileCPP( const sys_string& filename ) {
    utils::FileView view;
    std::ifstream file( filename.c_str(), std::ios::binary | std::ios::in );

    IF_RET( !file );

    file.seekg( 0, std::ios::end );
    view.size = ( size_t ) file.tellg();
    file.seekg( 0, std::ios::beg );

    IF_RET( !view.size );

    // growing buffer for each thread
    static thread_local utils::Buffer buffer;
    char* ptr = buffer.grow( view.size );

    file.read( ptr, view.size );

    // check first 100 bytes for binary
    IF_RET( !utils::isTextFile( std::string_view( ptr, std::min<size_t>( view.size, 100ul ) ) ) );

    view.lines = utils::parseContent( ptr, view.size );
    return view;
}

// C API with lseek instead fstat
utils::FileView fromFileLSeek( const sys_string& filename ) {
    utils::FileView view;
    FILE* file = fopen( filename.c_str(), O_RB );
    IF_RET( file == NULL );
    const utils::ScopeGuard onExit( [file] { fclose( file ); } );

    fseek( file, 0, SEEK_END );
    view.size = ftell( file );
    fseek( file, 0, SEEK_SET );

    IF_RET( !view.size );

    // growing buffer for each thread
    static thread_local utils::Buffer buffer;
    char* ptr = buffer.grow( view.size );

    IF_RET( view.size != fread( ptr, 1, view.size, file ) );

    // check first 100 bytes for binary
    IF_RET( !utils::isTextFile( std::string_view( ptr, std::min<size_t>( view.size, 100ul ) ) ) );

    view.lines = utils::parseContent( ptr, view.size );
    return view;
}

utils::FileView fromFileTwoFread( const sys_string& filename ) {
    utils::FileView view;
    FILE* file = fopen( filename.c_str(), O_RB );
    IF_RET( file == NULL );
    const utils::ScopeGuard onExit( [file] { fclose( file ); } );

    view.size = utils::fileSize( fileno( file ) );
    IF_RET( !view.size );

    // growing buffer for each thread
    static thread_local utils::Buffer buffer;
    char* ptr = buffer.grow( view.size );

    // check first 100 bytes for binary
    if( view.size > 100 ) {
        IF_RET( 100 != fread( ptr, 1, 100, file ) );
        IF_RET( !utils::isTextFile( std::string_view( ptr, 100 ) ) );
        IF_RET( view.size - 100 != fread( ptr + 100, 1, view.size - 100, file ) );
    } else {
        IF_RET( view.size != fread( ptr, 1, view.size, file ) );
        IF_RET( !utils::isTextFile( std::string_view( ptr, view.size ) ) );
    }

    view.lines = utils::parseContent( ptr, view.size );
    return view;
}

utils::FileView fromFileUtils( const sys_string& filename ) {
    return fromFileParser( filename, utils::parseContent );
}

utils::FileView fromFileForLoop( const sys_string& filename ) {
    return fromFileParser( filename, parseContentForLoop );
}

utils::FileView fromFileFind( const sys_string& filename ) {
    return fromFileParser( filename, parseContentFind );
}

using fromFileFunc = utils::FileView( const sys_string& filename );

std::map<fromFileFunc*, const char*> names = {
    {fromFilePosix, "fromFilePosix"},
#if !WIN32
    {fromFileMmap, "fromFileMmap"},
#endif
    {fromFileLocal, "fromFileLocal"},
    {fromFileString, "fromFileString"},
    {fromFileLSeek, "fromFileLSeek"},
    {fromFileTwoFread, "fromFileTwoFread"},
    {fromFileCPP, "fromFileCPP"},
    {fromFileUtils, "fromFileUtils"},
    {fromFileForLoop, "fromFileForLoop"},
    {fromFileFind, "fromFileFind"},
    {fromFileCPP, "fromFileCPP"},
    {utils::fromFileC, "utils::fromFileC"},
};

size_t run( fromFileFunc fromFile ) {
    size_t sum = 0;
    size_t lineCount = 0;
    size_t files = 0;

#if !WIN32
    fs::path include = "/usr/include";
#else
    fs::path include = std::string( getenv( "VS140COMNTOOLS" ) ) + "..\\..\\VC\\include";
#endif

    auto tp = std::chrono::system_clock::now();

    utils::recurseDir( include.native(), [&sum, &lineCount, &files, fromFile]( const sys_string & filename ) {
        auto view = fromFile( filename );
        files++;
        sum += view.size;
        lineCount += view.lines.size();
    } );

    auto duration = std::chrono::system_clock::now() - tp;
    std::chrono::milliseconds ms = std::chrono::duration_cast<std::chrono::milliseconds>( duration );

    printf( "%16s : %zu files, %5zu kB and %zu lines in %lld ms\n",
            names[fromFile], files, sum / 1024, lineCount, ms.count() );
    return ms.count();
}

BOOST_AUTO_TEST_SUITE( Performance )

BOOST_AUTO_TEST_CASE( Test_fromFile ) {

    size_t t2 = run( fromFileCPP );
#if !WIN32
    /*size_t tM = */run( fromFileMmap );
#endif
    /*size_t tS = */run( fromFileString );
    /*size_t tF = */run( fromFileLSeek );
    /*size_t tL = */run( fromFileLocal );
    /*size_t tO = */run( fromFileTwoFread );
    /*size_t tP = */run( fromFilePosix );
    size_t t1 = run( utils::fromFileC );
    printf( "\n" );
    BOOST_CHECK_LT( t1, t2 ); // assume FILE* is faster than std::ifstream
}

BOOST_AUTO_TEST_CASE( Test_parseContent ) {
    size_t t2 = run( fromFileForLoop );
    size_t t1 = run( fromFileUtils );
    size_t tf = run( fromFileFind );
    printf( "\n" );
    BOOST_CHECK_LT( t1, t2 );
}

BOOST_AUTO_TEST_CASE( Test_ThreadPool ) {
    boost::int_least64_t ns_asio = 0;
    boost::int_least64_t ns_own = 0;
    std::atomic_int counter = 0;
    {
        boost::timer::cpu_timer stopwatch;
        stopwatch.start();
        {
            boost::asio::thread_pool pool( std::min( std::thread::hardware_concurrency(), 8u ) );

            for( int i = 0; i < 1000; ++i ) {
                boost::asio::post( pool, [&counter] {counter++;} );
            }

            pool.join();
        }

        stopwatch.stop();
        ns_asio = stopwatch.elapsed().wall;
    }
    BOOST_REQUIRE_EQUAL( counter, 1000 );
    {
        boost::timer::cpu_timer stopwatch;
        stopwatch.start();
        {
            ThreadPool pool( std::min( std::thread::hardware_concurrency(), 8u ) );

            for( int i = 0; i < 1000; ++i ) {
                pool.add( [&counter] {counter++;} );
            }
        }

        stopwatch.stop();
        ns_own = stopwatch.elapsed().wall;
    }
    BOOST_REQUIRE_EQUAL( counter, 2000 );

    printf( "own %llu us, boost %llu us\n\n", ns_own / 1000, ns_asio / 1000 );
    BOOST_CHECK_LT( ns_asio, ns_own ); // assume own tp is slower than boost::asio
}

boost::int_least64_t timed1000( const std::string& name, const std::function<void()>& func ) {
    boost::timer::cpu_timer stopwatch;
    stopwatch.start();

    for( int i = 0; i < 1000; ++i ) {
        func();
    }

    stopwatch.stop();
    boost::int_least64_t ns = stopwatch.elapsed().wall;
    std::cout << name << " : " << ns / 1000 << " us" << std::endl;
    return ns;
}

BOOST_AUTO_TEST_CASE( Test_printf ) {
    std::string text = "text123";
#if WIN32
    FILE* file = fopen( L"dump.txt", L"w" );
#else
    FILE* file = fopen( "dump.txt", "w" );
#endif

    /*boost::int_least64_t t_write = */timed1000( "write", [file, text] {
        std::string data = "[" + text + "]\n";
        write( fileno( file ), data.c_str(), data.size() );
    } );

    fseek( file, 0, SEEK_SET );

    boost::int_least64_t t_printf = timed1000( "fprintf", [file, text] {
        fprintf( file, "%s%s]\n", "[", text.c_str() );
    } );

    fseek( file, 0, SEEK_SET );

    /*boost::int_least64_t t_fputs = */timed1000( "fputs", [file, text] {
        fputs( ( "[" + text + "]\n" ).c_str(), file );
    } );

    fseek( file, 0, SEEK_SET );

    boost::int_least64_t t_fwrite = timed1000( "fwrite", [file, text] {
        std::string data = "[" + text + "]\n";
        fwrite( data.c_str(), 1, data.size(), file );
    } );

    fseek( file, 0, SEEK_SET );

    fclose( file );
    BOOST_CHECK_LT( t_fwrite, t_printf ); // assume fwrite is faster than printf
    printf( "\n" );
}

BOOST_AUTO_TEST_CASE( Test_find ) {
    std::string text = "You can get there from here, but why on earth would you want to?";
    std::string term = "earth";
    size_t pos;
    void* ptr;
    std::string::iterator it;
    std::string::iterator it2;
    boost::algorithm::boyer_moore_horspool bmh( term.begin(), term.end() );
    boost::algorithm::knuth_morris_pratt kmp( term.begin(), term.end() );

    boost::int_least64_t t_find = timed1000( "find", [&text, &term, &pos] {
        pos = text.find( term );
    } );

#if !WIN32
    boost::int_least64_t t_memmem = timed1000( "memmem", [&text, &term, &ptr] {
        ptr = memmem( text.data(), text.size(), term.data(), term.size() );
    } );
#endif

    boost::int_least64_t t_strstr = timed1000( "strstr", [&text, &term, &ptr] {
        ptr = ( void* )strstr( text.data(), term.data() );
    } );

    boost::int_least64_t t_BMH = timed1000( "boyer_moore_horspool_search", [&text, &term, &it, &bmh] {
        it = bmh( text.begin(), text.end() ).first;
    } );

    boost::int_least64_t t_KMP = timed1000( "knuth_morris_pratt_search", [&text, &term, &it2, &kmp] {
        it2 = kmp( text.begin(), text.end() ).first;
    } );

    BOOST_REQUIRE_NE( pos, std::string::npos );
    BOOST_REQUIRE_NE( ptr, nullptr );

    BOOST_CHECK_LT( t_find, t_strstr ); // assume find is faster than memmem
    BOOST_CHECK_LT( t_BMH, t_find ); // assume bmh is faster than find
    printf( "\n" );
}

BOOST_AUTO_TEST_SUITE_END()

