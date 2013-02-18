--TEST--
HTTP functionality
--DESCRIPTION--
This test verifies that we can run tests using the built in HTTP server
--FILE--
<?php
$ZO_HTTP_REQUEST = <<<REQUEST
var_dump(extension_loaded("Zend Optimizer+"));
REQUEST;

include_once("tests/server.include.php");
?>
--EXPECT--
bool(true)

