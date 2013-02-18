<?php
if (!defined ("ZO_SETUP") ) {
	define ("ZO_DIR",		dirname(__FILE__));
	define ("ZO_MODULE",	realpath(sprintf("%s/../modules/ZendOptimizerPlus.so", ZO_DIR)));
	define ("ZO_EXEC",		getenv("TEST_PHP_EXECUTABLE") ? getenv("TEST_PHP_EXECUTABLE") : `which php`);
	define ("ZO_ADDRESS", 	getenv("ZO_ADDRESS") ? getenv("ZO_ADDRESS") : "127.0.0.1");
	define ("ZO_PORT",		getenv("ZO_PORT") ? getenv("ZO_PORT") : "8000");
	define ("ZO_SETUP", 	true);
	
	/*
		Will write $code to ZO_ADDRESS:ZO_PORT/test.php and return the result of the request to that location
	*/
	function zo_serve($name, $code, &$response, &$status, $options = array()) {
		$name = basename($name);
		if (@file_put_contents(sprintf(
			"%s/%s", ZO_DIR, $name
		), sprintf(
			"<?\n%s\n?>", $code
		))) {
			if (($process = @proc_open(sprintf(
				"%s -d zend_extension=%s -S %s:%d -t %s &",
				ZO_EXEC, ZO_MODULE, ZO_ADDRESS, ZO_PORT, ZO_DIR
			), array(
			   0 => array("pipe", "r"),
			   1 => array("pipe", "w"),
			   2 => array("file", sprintf("%s/server.log", ZO_DIR), "a")
			), $pipes, ZO_DIR, null, $options))) {
				$status = @proc_get_status($process);
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

				/* because it's good practice */
				fclose($pipes[0]);	
				fclose($pipes[1]);
				
				/* in preparation for the next test */
				@proc_terminate($process);
				
				return strlen($response);
			}	
		}
	}
}

if ($ZO_HTTP_REQUEST) {
	if (zo_serve($ZO_HTTP_REQUEST_NAME, $ZO_HTTP_REQUEST, $response, $status)) {
		echo $response;
	} else var_dump($status);
}
?>
