<?php
/*
 The purpose of this file is to make use of an HTTP server in order to conduct regression testing for Zend Optimizer+

 The following example assumes you are executing in the root of the Zend Optimizer+ build directory:
	TEST_PHP_EXECUTABLE=/path/to/php /path/to/php run-tests.php

 The default behaviour is to use the built in HTTP server, to which the following applies:
 	The user executing the tests should have access to ZO_PORT as defined below, and ZO_ADDRESS should be a sensible IPv4 address ( probably loopback )
 	The server will log to server.log in this directory ( which must be writable by the process running the tests )

 Testing may also be conducted using an external server, to which the following applies: 	
	Set ZO_ADDRESS:ZO_PORT to the address and port that the external HTTP server is already bound too.
	Set ZO_DIR to the DocumentRoot of the external HTTP server ( which must be writable by the process conducting the tests )
	Set ZO_EXTERNAL in the environment ( any value )
	
 Other environment variables that are used:
 	ZO_CONFIG - the full path to a php configuration, should include zend_extension = ${ZO_MODULE} to load Zend Optimizer+

 Caution:
	It is not adviseable to use a production server, or even a public facing server to conduct testing; tests are designed to crash processes and fail !
*/
if (!defined ("ZO_SETUP") ) {
	/*
	 Should be the root of the test directory
	*/
	define ("ZO_DIR",		getenv("ZO_DIR") ? getenv("ZO_DIR") : dirname(__FILE__));
	/*
	 Should be a preconfigured php.ini loading Zend Optimizer+ from location injected into environment of server
	*/
	define ("ZO_CONFIG",	getenv("ZO_CONFIG") ? getenv("ZO_CONFIG") : sprintf("%s/testing.ini", dirname(__FILE__)));	
	/*
	 Should be the location of the module just compiled
	*/
	define ("ZO_MODULE",	realpath(sprintf("%s/../modules/ZendOptimizerPlus.so", dirname(__FILE__))));
	/*
	 Should be copied from TEST_PHP_EXECUTABLE or detected
	*/
	define ("ZO_EXEC",		getenv("TEST_PHP_EXECUTABLE") ? getenv("TEST_PHP_EXECUTABLE") : `which php`);
	/*
	 Should be the loopback address to listen on
	*/
	define ("ZO_ADDRESS", 	getenv("ZO_ADDRESS") ? getenv("ZO_ADDRESS") : "127.0.0.1");
	/*
	 Should be the port number to listen on that is high enough that normal users have access
	*/
	define ("ZO_PORT",		getenv("ZO_PORT") ? getenv("ZO_PORT") : "8000");
	/*
	 Should be defined if the server listening on ZO_ADDRESS:ZO_PORT with DocumentRoot of ZO_DIR is an external process
	*/
	define ("ZO_EXTERNAL",     getenv("ZO_EXTERNAL") ? true : false);
	/*
	 Should be the name of a file for cli-server logging
	*/
	define ("ZO_HTTP_LOG",	getenv("ZO_HTTP_LOG") ? getenv("ZO_HTTP_LOG") : sprintf("%s/server.log", dirname(__FILE__)));
	/*
	 Ensures these constants are not redefined
	*/
	define ("ZO_SETUP", 	true);
}

if (!function_exists("zo_serve")) {
	/*
		Will write $code to ZO_DIR/$name and return the result of the request to ZO_ADDRESS:ZO_PORT/$name
	*/
	function zo_serve($name, $code, &$response, &$status, $options = array()) {
		$name =     basename($name);		
		$output =   sprintf("%s/%s", ZO_DIR, $name);
		$request =  sprintf("http://%s:%d/%s", ZO_ADDRESS, ZO_PORT, $name);
		/* 
		 Here we write $code to output (overwriting the original test file so PHP cleans it up after make test
		*/
		if (@file_put_contents($output, sprintf("<?\n%s\n?>", $code))) {
			
			/*
				If ZO_EXTERNAL is not in environment then cli-server is started now
			*/
			if (ZO_EXTERNAL || ($process = @proc_open(sprintf(
				"%s -c %s -n -S %s:%d -t %s",
				ZO_EXEC, ZO_CONFIG, ZO_ADDRESS, ZO_PORT, ZO_DIR
			), array(
			   0 => array("pipe", "r+w"),
			   1 => array("pipe", "w"),
			   2 => array("file", ZO_HTTP_LOG, "a")
			), $pipes, ZO_DIR, array(
				"ZO_DIR" => ZO_DIR,
				"ZO_CONFIG" => ZO_CONFIG,
				"ZO_MODULE" => ZO_MODULE,
				"ZO_EXEC" => ZO_EXEC,
				"ZO_ADDRESS" => ZO_ADDRESS,
				"ZO_PORT" => ZO_PORT,
				"ZO_HTTP_LOG" => ZO_HTTP_LOG,
				"ZO_SETUP" => ZO_SETUP
			), $options))) {

				/* cli-server may need some time to startup */
				$tries = ZO_EXTERNAL ? 1 : 10;

				if (!ZO_EXTERNAL) {
					/* give cli-server a chance to start */
					usleep(10000);
				}

				while ($tries--) {
					/* check for a response */
					if (($response = @file_get_contents($request))) {
						break;
					} else usleep(50000);
				}
				
				if (!ZO_EXTERNAL) {
					/* close standard input of server */
					fclose($pipes[0]);
				
					/* send terminate */
					proc_terminate($process);
				
					/* cleanup */
					$status = stream_get_contents($pipes[1]);
				}
				
				/* return length of response */
				return strlen($response);

			} else $status = "failed to start server";
		} else $status = sprintf("could not write to %s", $output);
	}
}

/*
 To conduct testing over HTTP the variables $ZO_HTTP_REQUEST and $ZO_HTTP_REQUEST_NAME should be defined
 $ZO_HTTP_REQUEST_NAME should be set to __FILE__
 $ZO_HTTP_REQUEST should be set the the PHP code to execute over HTTP
*/
if ($ZO_HTTP_REQUEST && $ZO_HTTP_REQUEST_NAME) {
	if (zo_serve($ZO_HTTP_REQUEST_NAME, $ZO_HTTP_REQUEST, $response, $status)) {
		echo $response;
	} else die($status);
} else die("cannot zo_serve without \$ZO_HTTP_REQUEST_NAME and \$ZO_HTTP_REQUEST");
?>
