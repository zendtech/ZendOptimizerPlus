<?php
/*
 The purpose of this file is to make use of the built in HTTP server in order to conduct regression testing for Zend Optimizer+

 The following example assumes you are executing in the root of the Zend Optimizer+ build directory:
	TEST_PHP_EXECUTABLE=/path/to/php /path/to/php run-tests.php

 The user executing the tests should have access to ZO_PORT as defined below, and ZO_ADDRESS should be a sensible IPv4 address ( probably loopback )
 The server will log to server.log in this directory ( which should also be writable for the user running the tests
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
	 Ensures these constants are not redefined
	*/
	define ("ZO_SETUP", 	true);
}

/*
 The following function starts and stops (gracefully) the built in HTTP server
*/
if (!function_exists("zo_serve")) {
	/*
		Will write $code to ZO_ADDRESS:ZO_PORT/$name.php and return the result of the request to that location
	*/
	function zo_serve($name, $code, &$response, &$status, $options = array()) {
		$name = basename($name);
		if (@file_put_contents(sprintf(
			"%s/%s", ZO_DIR, $name
		), sprintf(
			"<?\n%s\n?>", $code
		))) {
			/*
			 The server is started using a custom configuration (bundled) with sensible options
			 This ensures not interference with existing php configurations elsewhere ( which will be ignored )
			*/
			if (($process = @proc_open(sprintf(
				"%s -c %s -n -S %s:%d -t %s",
				ZO_EXEC, ZO_CONFIG, ZO_ADDRESS, ZO_PORT, ZO_DIR
			), array(
			   0 => array("pipe", "r+w"),
			   1 => array("pipe", "w"),
			   2 => array("file", sprintf("%s/server.log", ZO_DIR), "a")
			), $pipes, ZO_DIR, array(
				"ZO_DIR" => ZO_DIR,
				"ZO_CONFIG" => ZO_CONFIG,
				"ZO_MODULE" => ZO_MODULE,
				"ZO_EXEC" => ZO_EXEC,
				"ZO_ADDRESS" => ZO_ADDRESS,
				"ZO_PORT" => ZO_PORT,
				"ZO_SETUP" => ZO_SETUP
			), $options))) {
				/* this is, as yet unused */
				$status = @proc_get_status($process);

				/* we should try the request a few times to give the server a chance to startup */
				$tries = 10;

				/* give cli-server a chance to start */
				usleep(10000);

				while ($tries--) {
					/* check for a response */
					if (($response = @file_get_contents(sprintf(
						"http://%s:%d/%s", ZO_ADDRESS, ZO_PORT, $name
					)))) {
						break;
					} else usleep(50000);
				}
				
				/* close standard input of server */
				fclose($pipes[0]);
				
				/* send terminate */
				proc_terminate($process);
				
				/* cleanup */
				$logging = stream_get_contents($pipes[1]);
				
				/* return length of response */
				return strlen($response);
			}	
		}
	}
}

if ($ZO_HTTP_REQUEST && $ZO_HTTP_REQUEST_NAME) {
	if (zo_serve($ZO_HTTP_REQUEST_NAME, $ZO_HTTP_REQUEST, $response, $status)) {
		echo $response;
	} else var_dump($status);
} else die("cannot zo_serve without \$ZO_HTTP_REQUEST_NAME and \$ZO_HTTP_REQUEST");
?>
