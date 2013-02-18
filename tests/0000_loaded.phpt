--TEST--
Test to verify the test suite will function
--DESCRIPTION--
This test verifies that make test is setup (-d zend_extension=modules/ZendOptimizerPlus.so)
--FILE--
<?php
var_dump(extension_loaded("Zend Optimizer+"));
?>
--EXPECT--
bool(true)

