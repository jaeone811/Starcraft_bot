"""Makes the JaeTankBot distribution zip (dist/JaeTankBot-<version>-win32.zip).

    python tools/make_package.py [--bwapi "C:/Users/me/BWAPI"] [--bots "C:/StarCraft/bwapi-data/AI"]

Build first (scripts/build_windows.bat). The zip holds:
  - the bot, its launcher and settings tool, the Korean guide and one-click setup
    (packaging/);
  - BWAPI 4.4.0's runtime files (LGPL-3.0, unmodified);
  - the opponent bots Steamhammer 5.3.6 and UAlbertaBot (MIT, unmodified), taken
    from --bots;
  - every license text (packaging/licenses/).
StarCraft itself is NOT included: StarCraft: Brood War 1.16.1 is Blizzard's game
and every player installs their own copy.
"""
import argparse
import os
import re
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RELEASE = os.path.join(ROOT, 'build', 'Release')
PACKAGING = os.path.join(ROOT, 'packaging')
HOME = os.environ.get('USERPROFILE', '')

# Opponent bots: files that go to bwapi-data/AI (all of a bot or none of it).
OPPONENTS = {
    'Steamhammer 5.3.6': ['Steamhammer.dll', 'Steamhammer_5.3.6.json'],
    'UAlbertaBot': ['UAlbertaBot.dll', 'UAlbertaBot_Config.txt'],
}


def crlf(text):
    return text.replace('\r\n', '\n').replace('\n', '\r\n')


def version():
    text = open(os.path.join(ROOT, 'CMakeLists.txt'), encoding='utf-8').read()
    return re.search(r'project\(JaeTankBot VERSION ([0-9.]+)', text).group(1)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--bwapi', default=os.path.join(HOME, 'BWAPI'),
                        help='BWAPI 4.4.0 install folder (has Starcraft/bwapi-data/BWAPI.dll)')
    parser.add_argument('--bots', default=os.path.join(HOME, 'Desktop', 'StarCraft', 'StarCraft', 'bwapi-data', 'AI'),
                        help='folder with the opponent bots (Steamhammer.dll, UAlbertaBot.dll and their settings)')
    args = parser.parse_args()
    v = version()
    top = 'JaeTankBot-%s' % v
    files = {
        'bwapi-data/AI/JaeTankBot.dll': os.path.join(RELEASE, 'JaeTankBot.dll'),
        'bwapi-data/AI/JaeTankBotPlay.exe': os.path.join(RELEASE, 'JaeTankBotPlay.exe'),
        'bwapi-data/AI/JaeTankBot.ini': os.path.join(ROOT, 'config', 'JaeTankBot.ini'),
        'tools/JaeTankBotConfig.exe': os.path.join(RELEASE, 'JaeTankBotConfig.exe'),
        'install.bat': os.path.join(ROOT, 'scripts', 'install_to_starcraft.bat'),
        'jaetankbot.ico': os.path.join(ROOT, 'resources', 'jaetankbot.ico'),
    }
    missing = [src for src in files.values() if not os.path.isfile(src)]
    if missing:
        raise SystemExit('missing (build first?): ' + ', '.join(missing))

    bw = os.path.join(args.bwapi, 'Starcraft')
    bwapi_files = {
        'BWAPI-4.4.0/bwapi-data/BWAPI.dll': os.path.join(bw, 'bwapi-data', 'BWAPI.dll'),
        'BWAPI-4.4.0/bwapi-data/bwapi.ini': os.path.join(bw, 'bwapi-data', 'bwapi.ini'),
        'BWAPI-4.4.0/bwapi-data/data/Broodwar.map': os.path.join(bw, 'bwapi-data', 'data', 'Broodwar.map'),
        'BWAPI-4.4.0/SNP_DirectIP.snp': os.path.join(bw, 'SNP_DirectIP.snp'),
    }
    if any(not os.path.isfile(src) for src in bwapi_files.values()):
        print('BWAPI files not found under', args.bwapi, '- the zip will not include BWAPI')
        bwapi_files = {}

    bot_files = {}
    for bot, names in OPPONENTS.items():
        paths = [os.path.join(args.bots, n) for n in names]
        if all(os.path.isfile(p) for p in paths):
            bot_files.update({'bwapi-data/AI/' + n: p for n, p in zip(names, paths)})
        else:
            print(bot, 'not found under', args.bots, '- left out')

    licenses = {'licenses/' + n: os.path.join(PACKAGING, 'licenses', n)
                for n in sorted(os.listdir(os.path.join(PACKAGING, 'licenses')))}

    os.makedirs(os.path.join(ROOT, 'dist'), exist_ok=True)
    out = os.path.join(ROOT, 'dist', top + '-win32.zip')
    if os.path.exists(out):
        os.remove(out)
    with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED) as z:
        for name, src in list(files.items()) + list(bwapi_files.items()) + list(bot_files.items()):
            z.write(src, top + '/' + name)
        for name, src in licenses.items():
            z.writestr(top + '/' + name, crlf(open(src, encoding='utf-8').read()).encode('utf-8-sig' if name.endswith('README.txt') else 'utf-8'))
        # The Korean guide (UTF-8 with BOM for Notepad) and the one-click setup.
        guide = open(os.path.join(PACKAGING, '실행방법.txt'), encoding='utf-8').read().replace('{version}', v)
        z.writestr(top + '/실행방법.txt', crlf(guide).encode('utf-8-sig'))
        setup = open(os.path.join(PACKAGING, '설치하기.bat'), encoding='utf-8').read()
        z.writestr(top + '/설치하기.bat', crlf(setup).encode('utf-8'))
        if bwapi_files:
            z.writestr(top + '/install_bwapi.bat', crlf(open(os.path.join(PACKAGING, 'install_bwapi.bat'), encoding='utf-8').read()))
    print('wrote', out)
    with zipfile.ZipFile(out) as z:
        for info in z.infolist():
            print('  %8d  %s' % (info.file_size, info.filename))


if __name__ == '__main__':
    main()
