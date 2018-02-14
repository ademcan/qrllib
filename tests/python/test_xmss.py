# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
from __future__ import print_function

from time import time
from unittest import TestCase

import pytest
from pyqrllib.pyqrllib import QRLDescriptor

from pyqrllib import pyqrllib


class TestXmssBasic(TestCase):
    def __init__(self, *args, **kwargs):
        super(TestXmssBasic, self).__init__(*args, **kwargs)

    def test_xmss_creation_height4(self):
        HEIGHT = 4
        seed = pyqrllib.ucharVector(48, 0)
        xmss = pyqrllib.XmssBasic(seed, HEIGHT)

        expected_address = "010274764b521b002b55c57fa182142310c0bd6f2be9b3d673bf2e7f731e86da45ed70fa3b21"
        expected_PK = "0102c25188b585f731c128e2b457069e" \
                      "afd1e3fa3961605af8c58a1aec4d82ac" \
                      "316d3191da3442686282b3d5160f25cf" \
                      "162a517fd2131f83fbf2698a58f9c46a" \
                      "fc5d"

        self.assertEqual(expected_PK, pyqrllib.bin2hstr(xmss.getPK()))
        self.assertEqual(expected_address, pyqrllib.bin2hstr(xmss.getAddress()))
        self.assertEqual(expected_address, pyqrllib.bin2hstr(pyqrllib.QRLHelper.getAddress(xmss.getPK())))

        descr = pyqrllib.QRLHelper.extractDescriptor(xmss.getPK())
        self.assertEqual(4, descr.getHeight())
        self.assertEqual(pyqrllib.SHAKE, descr.getHashFunction())

    def test_xmss_creation_height6(self):
        HEIGHT = 6
        seed = pyqrllib.ucharVector(48, 0)
        xmss = pyqrllib.XmssBasic(seed, HEIGHT)

        expected_address = "010327fe0d944d50c033fd4a9995b13a004017575db9f834d357463df66c68303edbdf0f4667"

        expected_PK = "0103859060f15adc3825adeec85c7483" \
                      "d868e898bc5117d0cff04ab1343916d4" \
                      "07af3191da3442686282b3d5160f25cf" \
                      "162a517fd2131f83fbf2698a58f9c46a" \
                      "fc5d"

        self.assertEqual(expected_PK, pyqrllib.bin2hstr(xmss.getPK()))
        self.assertEqual(expected_address, pyqrllib.bin2hstr(xmss.getAddress()))
        self.assertEqual(expected_address, pyqrllib.bin2hstr(pyqrllib.QRLHelper.getAddress(xmss.getPK())))

        descr = pyqrllib.QRLHelper.extractDescriptor(xmss.getPK())
        self.assertEqual(6, descr.getHeight())
        self.assertEqual(pyqrllib.SHAKE, descr.getHashFunction())

    def test_xmss(self):
        HEIGHT = 6

        seed = pyqrllib.ucharVector(48, 0)
        xmss = pyqrllib.XmssBasic(seed, HEIGHT)

        # print("Seed", len(seed))
        # print(pyqrllib.bin2hstr(seed, 48))
        #
        # print("PK  ", len(xmss.getPK()))
        # print(pyqrllib.bin2hstr(xmss.getPK(), 48))
        #
        # print("SK  ", len(xmss.getSK()))
        # print(pyqrllib.bin2hstr(xmss.getSK(), 48))

        self.assertIsNotNone(xmss)
        self.assertEqual(xmss.getHeight(), HEIGHT)

        message = pyqrllib.ucharVector([i for i in range(32)])
        # print("Msg ", len(message))
        # print(pyqrllib.bin2hstr(message, 48))

        # Sign message
        signature = bytearray(xmss.sign(message))

        # print("Sig ", len(signature))
        # print(pyqrllib.bin2hstr(signature, 128))
        #
        # print('----------------------------------------------------------------------')
        # Verify signature
        start = time()
        for i in range(1000):
            self.assertTrue(pyqrllib.XmssBasic.verify(message,
                                                      signature,
                                                      xmss.getPK()))
        end = time()
        # print(end - start)

        # Touch the signature
        signature[100] += 1
        self.assertFalse(pyqrllib.XmssBasic.verify(message,
                                                   signature,
                                                   xmss.getPK()))
        signature[100] -= 1
        self.assertTrue(pyqrllib.XmssBasic.verify(message,
                                                  signature,
                                                  xmss.getPK()))

        # Touch the message
        message[2] += 1
        self.assertFalse(pyqrllib.XmssBasic.verify(message,
                                                   signature,
                                                   xmss.getPK()))
        message[2] -= 1
        self.assertTrue(pyqrllib.XmssBasic.verify(message,
                                                  signature,
                                                  xmss.getPK()))

    def test_xmss_exception_constructor(self):
        HEIGHT = 7
        seed = pyqrllib.ucharVector(48, 0)

        with pytest.raises(ValueError):
            xmss = pyqrllib.XmssFast(seed, HEIGHT, pyqrllib.SHAKE)

    def test_xmss_exception_verify(self):
        message = pyqrllib.ucharVector(48, 0)
        signature = pyqrllib.ucharVector(2287, 0)
        pk = pyqrllib.ucharVector(48, 0)

        self.assertFalse(pyqrllib.XmssFast.verify(message, signature, pk))
