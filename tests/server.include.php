<?php
if (!defined ("ZO_SETUP") ) {
	define ("ZO_EXEC",		getenv("TEST_PHP_EXECUTABLE") ? getenv("TEST_PHP_EXECUTABLE") : `which php`);
	define ("ZO_ADDRESS", 	getenv("ZO_ADDRESS") ? getenv("ZO_ADDRESS") : "127.0.0.1");
	define ("ZO_PORT",		getenv("ZO_PORT") ? getenv("ZO_PORT") : "80");
	define ("ZO_SETUP", 	true);
}


?>
