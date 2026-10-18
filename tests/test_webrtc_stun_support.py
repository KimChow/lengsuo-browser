"""Independent fixed-byte checks for the loopback-only responder."""
import random
import unittest

from webrtc_stun_support import binding_response

REQUEST = bytes.fromhex('000100002112a442000102030405060708090a0b')
RESPONSE = bytes.fromhex(
    '0101000c2112a442000102030405060708090a0b'
    '002000080001bda3ea12d553')


class StunTests(unittest.TestCase):
    def test_independent_response_bytes(self):
        self.assertEqual(binding_response(REQUEST), RESPONSE)

    def test_zero_transaction_id_is_preserved(self):
        self.assertEqual(binding_response(REQUEST[:8] + bytes(12)),
                         RESPONSE[:8] + bytes(12) + RESPONSE[20:])

    def test_padded_attribute(self):
        request = REQUEST[:2] + bytes.fromhex('0008') + REQUEST[4:]
        self.assertEqual(binding_response(request + bytes.fromhex(
            '8022000100000000')), RESPONSE)

    def test_reject_header_and_length_errors(self):
        bad = [b'', REQUEST[:19], b'\x01\x01' + REQUEST[2:],
               REQUEST[:4] + bytes(4) + REQUEST[8:],
               REQUEST + bytes(4), REQUEST[:2] + b'\x00\x01' +
               REQUEST[4:] + b'\x00', REQUEST + bytes(2048)]
        for request in bad:
            with self.subTest(request=request[:20]):
                self.assertIsNone(binding_response(request))

    def test_reject_truncated_attribute(self):
        request = REQUEST[:2] + bytes.fromhex('0004') + REQUEST[4:]
        self.assertIsNone(binding_response(request + bytes.fromhex('80220008')))

    def test_fixed_seed_untrusted_datagrams(self):
        rng = random.Random(381)
        for _ in range(1000):
            request = rng.randbytes(rng.randrange(2300))
            self.assertIsNone(binding_response(request))


if __name__ == '__main__':
    unittest.main()
