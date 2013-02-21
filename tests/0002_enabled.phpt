--TEST--
HTTP enabled
--DESCRIPTION--
Test that O+ is enabled in embedded cli-server
--FILE--
<?php
$ZO_HTTP_REQUEST_NAME = __FILE__;
$ZO_HTTP_REQUEST = <<<REQUEST
\$status = accelerator_get_status();
if (\$status["accelerator_enabled"]) {
	printf("OK");
} else printf("FAIL");
REQUEST;

include_once("tests/server.include.php");
?>
--EXPECT--
OK
