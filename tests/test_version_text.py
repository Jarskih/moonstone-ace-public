"""Host test of include/game/version_text.hpp: the title menu's version string ("V0.2"), and that the title menu draws it."""
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CXX = shutil.which('clang++')

DRIVER = r'''
#include <stdio.h>
#include "game/version_text.hpp"
int main() {
	char b[ms::game::VERSION_TEXT_MAX];
	static const unsigned cases[][2] = {{0,2},{1,10},{0,0},{12,3},{255,255},{2,100},{10,9}};
	for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
		unsigned n = ms::game::versionText(b, (uint8_t)cases[i][0], (uint8_t)cases[i][1]);
		printf("%s %u\n", b, n);
	}
	unsigned n = ms::game::versionText(b);
	printf("cur %s %u %u %u\n", b, n, (unsigned)ms::GAME_VERSION_MAJOR, (unsigned)ms::GAME_VERSION_MINOR);
	return 0;
}
'''


@unittest.skipUnless(CXX, 'clang++ not found')
class VersionTextTest(unittest.TestCase):
    def test_format(self):
        with tempfile.TemporaryDirectory() as d:
            src = os.path.join(d, 'd.cpp')
            exe = os.path.join(d, 'd.exe')
            with open(src, 'w') as f:
                f.write(DRIVER)
            subprocess.check_call([CXX, '-std=c++17', '-fno-exceptions', '-fno-rtti', '-I', os.path.join(ROOT, 'include'), src, '-o', exe])
            out = subprocess.check_output([exe], text=True).split('\n')
        self.assertEqual(out[:7], ['V0.2 4', 'V1.10 5', 'V0.0 4', 'V12.3 5', 'V255.255 8', 'V2.100 6', 'V10.9 5'])
        parts = out[7].split()
        self.assertEqual(parts[1], 'V%s.%s' % (parts[3], parts[4]))   # the current-version overload uses the engine constants

    def test_title_menu_draws_it_from_the_constants(self):
        text = open(os.path.join(ROOT, 'src', 'rt', 'scene_menu.cpp'), encoding='utf-8', errors='replace').read()
        self.assertIn('versionText(acVersion)', text)
        self.assertNotIn('"V0.', text)


if __name__ == '__main__':
    unittest.main()
