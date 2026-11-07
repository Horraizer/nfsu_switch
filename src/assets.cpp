#include "nfsu/assets.h"

#include "nfsu/sha256.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <utility>
#include <vector>

// The retail manifests and the check; include/nfsu/assets.h is the
// contract and docs/SWITCH_ASSETS.md the reasons. Standard library, the
// SHA-256 of nfsu_sha256 and the FileSystemService only: nothing here opens
// a file itself.

namespace nfsu {

namespace {

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// The retail files
// ---------------------------------------------------------------------------

// Sizes and SHA-256 hashes read from the owner's retail installation, each
// file dated 2003 there. Two files of that installation are 2025
// modifications - Global/GlobalB.lzc and Global/GLOBALA.BUN - and neither is
// in any set: the port reads GLOBALB.BUN, the retail image of GLOBALB.LZC,
// and never reads GLOBALA.BUN. docs/SWITCH_ASSETS.md records both.
struct RetailFile {
    const char* path;
    std::uint64_t size;
    const char* sha256;
    const char* purpose;
};

constexpr RetailFile kRetailFiles[] = {
    {"Cars/240SX/GEOMETRY.BIN", 4780816,
     "002202192752838E1B65E9B7C219EAB381FD5F916C3D031DDE93428EB325F619",
     "the 240SX's meshes"},
    {"Cars/240SX/TEXTURES.BIN", 697340,
     "725DC56CB4807FF8E8E96F1D667F5FDF8DC5ADE3C7D06E759FC62E7DEEFDF6BA",
     "the 240SX's textures and its doorline skin layer"},
    {"Cars/BRAKES/GEOMETRY.BIN", 69408,
     "FEC4CBF4F9C3E7FB1CD883F0B874F920248652A2E63EE4A341446563F0B9B1A5",
     "the brake meshes the stock car places"},
    {"Cars/PLATES/GEOMETRY.BIN", 1184,
     "0EBCFD14DD303613F5DA3FF5D7A6DF57BE3CDE915F458BA243FE5D089B64BD8A",
     "the licence plate mesh"},
    {"Cars/TEXTURES.BIN", 1230808,
     "999EC47CC816CADA2158317A46E00023832031B3A4A35DDCBA31F5FBD2F9EB58",
     "the shared car textures, with the skin target DUMMY_SKIN1"},
    {"FrontEnd/FrontB.lzc", 4182578,
     "76F368D5E5E30F96A8182BF457DBA4491395E075C3F336D86A32012B56E16F90",
     "the career event table: event 1's track, laps and direction"},
    {"Global/DYNTEX.BIN", 4677572,
     "020E99EBEA6A4AEFC7F7C858BA8672A48FA040E00299E2372E914687BF90525C",
     "the in-race tachometer LINES and RED faces"},
    {"Global/GLOBALB.BUN", 2361176,
     "39DF38FAC2BE7CA0F0D0A92E0113CAAEDA5B1C60E14436C612B3D0874AFC8DB9",
     "the car part database, the car and track tables, shared textures"},
    {"Global/INGAMEB.BUN", 1670080,
     "044724AB4C167831587E80F29961A1E7D8404C9DD5266533F3563C555A997299",
     "the in-race textures the track draws with"},
    {"Languages/LanguageTextures.bin", 517284,
     "6A92604D9766557115DEE8F8CE18F392557AF1A8B0369E69B474F36DF2691615",
     "the in-race LCD glyphs"},
    {"Tracks/STREAML1RA.BUN", 92394688,
     "2385556EC31142F7390434CA70152A9F3D629155AF991BB6595422FCB26A752E",
     "the L1RA stream: every section of track 1003"},
    {"Tracks/STREAML1RA_1003.BUN", 0,
     "E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855",
     "track 1003's stream pack, empty in the retail game, which names it"},
    {"Tracks/TEX1003TRACK.BIN", 1086216,
     "081B4D966C154D7B9823E70DD5031269A8D3293F8CD13ED515EF644F09573ED5",
     "track 1003's sky and track map textures"},
    {"Tracks/TRACKB1003.lzc", 433352,
     "ADA8206B5D66E3286BF582E2C7117FE0CC2BA9F6CADB756D922F7D32E7C696BA",
     "track 1003's streaming table, zones, route and collision"},
    {"Tracks/STREAML1RA_1005.BUN", 0,
     "E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855",
     "track 1005's empty retail stream pack"},
    {"Tracks/TEX1005TRACK.BIN", 1086216,
     "CD86FEDB15B5F4CBC3E2180CBE0DECD855F7F58AACF00454B13CC175404976E1",
     "track 1005's sky and track map textures"},
    {"Tracks/TRACKB1005.lzc", 423196,
     "635C53E038FA7CBE0F90B00F8089DD3D7FAD7135433AD678663EED2F4371E8A5",
     "track 1005's streaming table, zones, route and collision"},
    {"Tracks/STREAML1RA_1001.BUN", 0,
     "E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855",
     "track 1001's empty retail stream pack"},
    {"Tracks/TEX1001TRACK.BIN", 965896,
     "4413BC81488B39AF4938DC21A18C7E6DFDD67C8C6AD8FF06111CC7288D4CD5F2",
     "track 1001's sky and track map textures"},
    {"Tracks/TRACKB1001.lzc", 556152,
     "E23BC0A7AFE09355A8DCF6E5845029958CE137821CF9EEB74A329C9EEEC5D078",
     "track 1001's streaming table, zones, route and collision"},
    {"Tracks/STREAML1RA_1002.BUN", 0,
     "E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855",
     "track 1002's empty retail stream pack"},
    {"Tracks/TEX1002TRACK.BIN", 1086216,
     "6E25069AFF95EB916A1B3A2E61543C4541C211C9878A56392A1356D90F0152B9",
     "track 1002's sky and track map textures"},
    {"Tracks/TRACKB1002.lzc", 482412,
     "5EA082F740CD51DE4393CF069C6DDAF2B1F88349441020C4A100F3180CCD6FD2",
     "track 1002's streaming table, zones, route and collision"},
    {"Tracks/STREAML1RA_1004.BUN", 0,
     "E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855",
     "track 1004's empty retail stream pack"},
    {"Tracks/TEX1004TRACK.BIN", 1086216,
     "6762C9DF0A28914363E382E6A19FF4C80A11F57424EBB7C1F3A50C7383B16F5E",
     "track 1004's sky and track map textures"},
    {"Tracks/TRACKB1004.lzc", 441134,
     "64B2FD197F8B9F889FE7FE3BBC7EAAD3378CD3BC31F641C8ECC469A479D23B5A",
     "track 1004's streaming table, zones, route and collision"},
    {"Tracks/STREAML1RA_1006.BUN", 0,
     "E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855",
     "track 1006's empty retail stream pack"},
    {"Tracks/TEX1006TRACK.BIN", 1086216,
     "DD594EED2FEF4E1C1A7023395D73E1FF196C9BA40F88B3A73116C4E852F08A08",
     "track 1006's sky and track map textures"},
    {"Tracks/TRACKB1006.lzc", 449789,
     "50C6E2A7717E28042143CC6122B5D81088946737D69DF3E3310D3F8A5B3EF68B",
     "track 1006's streaming table, zones, route and collision"},
    {"Tracks/STREAML1RA_1007.BUN", 0,
     "E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855",
     "track 1007's empty retail stream pack"},
    {"Tracks/TEX1007TRACK.BIN", 965896,
     "3C6CA522F5BA02D23F9A816C33CC3D132CF0E64383F58D2C203856E583E5D43B",
     "track 1007's sky and track map textures"},
    {"Tracks/TRACKB1007.lzc", 402183,
     "80E7BE085D8A59FB0698FF68A9D9C58FD3EA1D7E94370E9B46332E1F7B30653A",
     "track 1007's streaming table, zones, route and collision"},
    {"Tracks/STREAML1RA_1008.BUN", 0,
     "E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855",
     "track 1008's empty retail stream pack"},
    {"Tracks/TEX1008TRACK.BIN", 965896,
     "A189130ACC3EDE1C74796C63992C7134B8336560470290AA33E14EA1091EC597",
     "track 1008's sky and track map textures"},
    {"Tracks/TRACKB1008.lzc", 398153,
     "19E5701C48F7E0B53B37E839B618BD51AF3F51D2E18D4B551684E7738AB2B3C5",
     "track 1008's streaming table, zones, route and collision"},
    {"Tracks/STREAML1RB.BUN", 30675616,
     "2C960E19F2EFE9DD260C2E4AFFEF53205B49F666DEF0D0E9BC98DA373065FCC4",
     "the L1RB stream: the city sections the drift tracks race"},
    {"Tracks/STREAML1RB_1301.BUN", 4309396,
     "2957029C7BF6854FF1693CEB13491C24DF3B790EFF2CA3332F934D63B82CF815",
     "track 1301's stream pack, with content of its own"},
    {"Tracks/TEX1301TRACK.BIN", 1086216,
     "4B43ADB08D438217B6A5012F8BA80E8766A549B8C7F1391DFBC9F9804C1698F5",
     "track 1301's sky and track map textures"},
    {"Tracks/TRACKB1301.lzc", 66720,
     "6DEF53D7FF9D9246ADDCA8DFE266CF5A42A08B083232A3BC2B8ECBB5C29B5F19",
     "track 1301's streaming table, zones, route and collision"},
    {"Tracks/STREAML1RC.BUN", 28523156,
     "D07DFD642B0605DC9CE2BC0274DB74A02C9B06BC4EB12115B06303A078CD4955",
     "the L1RC stream: the city sections the later drift tracks race"},
    {"Tracks/STREAML1RB_1302.BUN", 4309396,
     "2957029C7BF6854FF1693CEB13491C24DF3B790EFF2CA3332F934D63B82CF815",
     "track 1302's stream pack, the shared drift-city image of L1RB"},
    {"Tracks/TEX1302TRACK.BIN", 1086216,
     "D73DDA7248C71089E0B4E5579D3C9FD7D54BA74D3A9BA8C071C6E4F5150EAE21",
     "track 1302's sky and track map textures"},
    {"Tracks/TRACKB1302.lzc", 77515,
     "EFE62EAFDA9AF079A6AEB7DF19D1CB67359AC292558541DCA6B0B98FD629EA1A",
     "track 1302's streaming table, zones, route and collision"},
    {"Tracks/STREAML1RB_1303.BUN", 4309396,
     "2957029C7BF6854FF1693CEB13491C24DF3B790EFF2CA3332F934D63B82CF815",
     "track 1303's stream pack, the shared drift-city image of L1RB"},
    {"Tracks/TEX1303TRACK.BIN", 1086216,
     "71E829016607CD3FA02303E265158FA9C4265F88B338C3634AC72647AFA256F6",
     "track 1303's sky and track map textures"},
    {"Tracks/TRACKB1303.lzc", 102735,
     "D9A2CD6F3798F0EB9F086219691217D18CD7FA57A54CD568FE115A30B2213E15",
     "track 1303's streaming table, zones, route and collision"},
    {"Tracks/STREAML1RB_1304.BUN", 4309396,
     "2957029C7BF6854FF1693CEB13491C24DF3B790EFF2CA3332F934D63B82CF815",
     "track 1304's stream pack, the shared drift-city image of L1RB"},
    {"Tracks/TEX1304TRACK.BIN", 1086216,
     "F0F2D47C4AE46A2BB8B60D01B0B2E42637D56F767929E05D2F9926E030AB815E",
     "track 1304's sky and track map textures"},
    {"Tracks/TRACKB1304.lzc", 126698,
     "6C9F45BDE1AD6A7F02798B92A645A1AAA209C10AF850FB5706A706861A119CEC",
     "track 1304's streaming table, zones, route and collision"},
    {"Tracks/STREAML1RC_1305.BUN", 1967428,
     "78E3CEF56F879D2DB9B2CCC699F2E2581059A802F361F6417DDE091CF5FF7435",
     "track 1305's stream pack, the shared drift-city image of L1RC"},
    {"Tracks/TEX1305TRACK.BIN", 1086216,
     "EE062A3DF00BE8440C3DF6E677AEC111DC11C49B422866399074A3E825ECDCFD",
     "track 1305's sky and track map textures"},
    {"Tracks/TRACKB1305.lzc", 116651,
     "BC325D8A18C518CF47D6B69244D0541136DAC78424EFD033A9E98A97DDA5D306",
     "track 1305's streaming table, zones, route and collision"},
    {"Tracks/STREAML1RC_1306.BUN", 1967428,
     "78E3CEF56F879D2DB9B2CCC699F2E2581059A802F361F6417DDE091CF5FF7435",
     "track 1306's stream pack, the shared drift-city image of L1RC"},
    {"Tracks/TEX1306TRACK.BIN", 1086216,
     "B0A15097BC23E1A03E85AE264F25BDE4A67C0F34C3935B1CA9195F8B653313FB",
     "track 1306's sky and track map textures"},
    {"Tracks/TRACKB1306.lzc", 135200,
     "AA67B72106AF7590823E1EEBF8AEAC48E4D31AA226583E5968CD83AAA4F72B5E",
     "track 1306's streaming table, zones, route and collision"},
    {"Tracks/STREAML1RC_1307.BUN", 1967428,
     "78E3CEF56F879D2DB9B2CCC699F2E2581059A802F361F6417DDE091CF5FF7435",
     "track 1307's stream pack, the shared drift-city image of L1RC"},
    {"Tracks/TEX1307TRACK.BIN", 1086216,
     "AA8ABE6085ADDCB88801B94BC96AB6EEBF3BD0467377BB29A53BED3595CE8909",
     "track 1307's sky and track map textures"},
    {"Tracks/TRACKB1307.lzc", 116051,
     "AD5C09F7FAC1879496D48C3F884882BCED57694BBA18ABE1A1ADBB2BC8A6B6D0",
     "track 1307's streaming table, zones, route and collision"},
    {"Tracks/STREAML1RC_1308.BUN", 1967428,
     "78E3CEF56F879D2DB9B2CCC699F2E2581059A802F361F6417DDE091CF5FF7435",
     "track 1308's stream pack, the shared drift-city image of L1RC"},
    {"Tracks/TEX1308TRACK.BIN", 1086216,
     "C6145711E2F0BDE45DDAE1296A5BFC30CAB6D075BB55FD5439009F18E2E23FEA",
     "track 1308's sky and track map textures"},
    {"Tracks/TRACKB1308.lzc", 111994,
     "145BEF2A4877604FA22FE77CA2C669B512DE1E2B518B0FEE3A3CEAEA6F4D4ECB",
     "track 1308's streaming table, zones, route and collision"},
    {"Tracks/STREAML1RE.BUN", 21216288,
     "18D66F11DE44B410CFCBF003CCF6E18EEB8FA6FB0CE6A6A5D8A83EE066258B55",
     "the L1RE stream: the city sections drag track 1207 races"},
    {"Tracks/STREAML1RB_1201.BUN", 8206776,
     "45CFE82A4DFD0A2131CE86D680D715D8C290A327E934CC8FB321536741AD40F7",
     "drag track 1201's stream pack, shared image with 1210"},
    {"Tracks/TEX1201TRACK.BIN", 1086216,
     "7D1029966EF62001B182053680389EC8D671C177FEC8674FCB5183D07EB9370E",
     "track 1201's sky and track map textures"},
    {"Tracks/TRACKB1201.lzc", 246347,
     "DCF7F514284F651A0E088E0E072EBBFD3E837039AED15B33D523E3CE420B026D",
     "track 1201's streaming table, zones, route and collision"},
    {"Tracks/STREAML1RC_1202.BUN", 8028272,
     "DCE8B03FAD85872350B26410C70EA991D6B4DA7655DFB79C599F898AF47949CD",
     "drag track 1202's stream pack"},
    {"Tracks/TEX1202TRACK.BIN", 823816,
     "C7ED91B4D6FA748951C242FA3A855B82557BFC87EE80FDA696DC4499A1C40C50",
     "track 1202's sky and track map textures"},
    {"Tracks/TRACKB1202.lzc", 200976,
     "5FE84A3F4EEA1A6F0C16EC7BB1A58D91F48D659802AE0ADCD42153BCFF85B7EE",
     "track 1202's streaming table, zones, route and collision"},
    {"Tracks/STREAML1RB_1206.BUN", 6728336,
     "5633BD7D99F59D323F3B90643F5FFC20C76E148B2042596BCBCDBA11CD58DF4A",
     "drag track 1206's stream pack, shared image with 1214"},
    {"Tracks/TEX1206TRACK.BIN", 823816,
     "F77B30C0FA4C74A648C5E65074079366222702C73BAB1E004A33F8F1F39A514C",
     "track 1206's sky and track map textures"},
    {"Tracks/TRACKB1206.lzc", 222035,
     "FB383DDD8CBDF5CE3785AE9100BB169B868A2FC4820E3A17B2BEFAB51568DD9F",
     "track 1206's streaming table, zones, route and collision"},
    {"Tracks/STREAML1RE_1207.BUN", 4694596,
     "B16FCA124717BDC18DFDD9D87498FBD0F6200E87F02B442AC3A2317BA038E74B",
     "drag track 1207's stream pack"},
    {"Tracks/TEX1207TRACK.BIN", 823816,
     "9BD68C62FBBD52DD5DA3A27A2ED9765B1F0AB9D215BE7FC08AEF7065C5A79FF3",
     "track 1207's sky and track map textures"},
    {"Tracks/TRACKB1207.lzc", 188398,
     "7F16F9938B017A42D4AC4222663AF8F5DF679ECF5CA60EAF6EAA9F8405BE11AA",
     "track 1207's streaming table, zones, route and collision"},
    {"Tracks/STREAML1RB_1210.BUN", 8206776,
     "45CFE82A4DFD0A2131CE86D680D715D8C290A327E934CC8FB321536741AD40F7",
     "drag track 1210's stream pack, shared image with 1201"},
    {"Tracks/TEX1210TRACK.BIN", 823816,
     "9C11334DA34F21917DC77757A78374D5DF876739E44D0869F1B828ACE0CFB948",
     "track 1210's sky and track map textures"},
    {"Tracks/TRACKB1210.lzc", 244396,
     "3EF7DDACDA744DD1F5C610AEDD791A5D4FA5E95C950C340BBE9B6F31D7370B07",
     "track 1210's streaming table, zones, route and collision"},
    {"Tracks/STREAML1RB_1214.BUN", 6728336,
     "5633BD7D99F59D323F3B90643F5FFC20C76E148B2042596BCBCDBA11CD58DF4A",
     "drag track 1214's stream pack, shared image with 1206"},
    {"Tracks/TEX1214TRACK.BIN", 823816,
     "076509FFA4E408B33A8096E1FE0215B469627A6CE4092EB34AFD63C2CF4C9EA4",
     "track 1214's sky and track map textures"},
    {"Tracks/TRACKB1214.lzc", 218065,
     "4B33662E4281BFB0623E15A6B08C04B3FDE0116A06A654D6CFBE4BE93FAF75B8",
     "track 1214's streaming table, zones, route and collision"},
    {"Tracks/STREAML1RA_1102.BUN", 0,
     "E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855",
     "sprint track 1102's empty retail stream pack"},
    {"Tracks/TEX1102TRACK.BIN", 1086216,
     "7625EECF2816D6EBD79A9F6E523EAB337C22D4B500FBBCB46F292E2D8EB24A09",
     "track 1102's sky and track map textures"},
    {"Tracks/TRACKB1102.lzc", 644372,
     "A60B908AA5C054A258788120C9AB4B1A2CB584FD960365A6316F0F365BADC4F9",
     "track 1102's streaming table, zones, route and collision"},
    {"Tracks/STREAML1RA_1103.BUN", 0,
     "E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855",
     "sprint track 1103's empty retail stream pack"},
    {"Tracks/TEX1103TRACK.BIN", 1086216,
     "F5CBFE5442C153FA314DE7BB18F5F180062B6C3E83F95D4D4C688D8E379AA49D",
     "track 1103's sky and track map textures"},
    {"Tracks/TRACKB1103.lzc", 718765,
     "C22E35ED7C91A9467691B3A9896CD0BDF13F91DA155C2E3912F88E567E4FF2B8",
     "track 1103's streaming table, zones, route and collision"},
    {"Tracks/STREAML1RA_1104.BUN", 0,
     "E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855",
     "sprint track 1104's empty retail stream pack"},
    {"Tracks/TEX1104TRACK.BIN", 1086216,
     "262D2A55EAEE43F7F90BC9CCB5C1DAE1E1A5E9FCBE81F2B50F1053AAB78FDDF4",
     "track 1104's sky and track map textures"},
    {"Tracks/TRACKB1104.lzc", 873077,
     "7D689551AF173BD4D7C04C0F7A224414D9A2FD2D80A23B7BEDEDB60526CC9F00",
     "track 1104's streaming table, zones, route and collision"},
    {"Tracks/STREAML1RA_1105.BUN", 0,
     "E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855",
     "sprint track 1105's empty retail stream pack"},
    {"Tracks/TEX1105TRACK.BIN", 1086216,
     "479712F491953A3A6B42C269AB4D6331CC5E7361105DDF30CDCF9F272487EC11",
     "track 1105's sky and track map textures"},
    {"Tracks/TRACKB1105.lzc", 853671,
     "0A26D41A043180391236D5A14A00F12C16D58B8BAFA0AAFF9FE7892E162C8CE3",
     "track 1105's streaming table, zones, route and collision"},
    {"Tracks/STREAML1RA_1106.BUN", 0,
     "E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855",
     "sprint track 1106's empty retail stream pack"},
    {"Tracks/TEX1106TRACK.BIN", 1086216,
     "FCBBAE765068F0809AED8F114399F59DF202CCA0C7E9D769EEF0D3877EBDAC5A",
     "track 1106's sky and track map textures"},
    {"Tracks/TRACKB1106.lzc", 927849,
     "FC772389B794008002F70E2EFBD528E78E065CFA9FAE6763E3FA6019432D4172",
     "track 1106's streaming table, zones, route and collision"},
    {"Tracks/STREAML1RA_1107.BUN", 0,
     "E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855",
     "sprint track 1107's empty retail stream pack"},
    {"Tracks/TEX1107TRACK.BIN", 1074952,
     "8BB7992EB4A0BE73971D3725C60DF775613ACA6BC4E859B53FC43645B3930BF0",
     "track 1107's sky and track map textures"},
    {"Tracks/TRACKB1107.lzc", 1005788,
     "06E9055955A61B6FAFE58355921B54B9FF2E758200DE74CC61FD8291F9FA5177",
     "track 1107's streaming table, zones, route and collision"},
    {"Tracks/STREAML1RA_1108.BUN", 0,
     "E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855",
     "sprint track 1108's empty retail stream pack"},
    {"Tracks/TEX1108TRACK.BIN", 1086216,
     "6E4365C4987F85E46F74B361C04D294DD44FBE9FACA8D303046D29FCB76986DF",
     "track 1108's sky and track map textures"},
    {"Tracks/TRACKB1108.lzc", 822136,
     "4024099C291D68D4DFE81B9FD1B4118ED50DD4217C147B90BC5A07042E535B60",
     "track 1108's streaming table, zones, route and collision"},
    {"Tracks/STREAML1RA_1109.BUN", 0,
     "E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855",
     "sprint track 1109's empty retail stream pack"},
    {"Tracks/TEX1109TRACK.BIN", 1086216,
     "18B68CCC8D26A0F27451DA9B83A13B2F037A1A6499543F625BE7B3065A9DB47C",
     "track 1109's sky and track map textures"},
    {"Tracks/TRACKB1109.lzc", 744101,
     "4714E5D4AA2CCFBC5A51740442F0BEC110CAA74D09195E1F20BE188309D09E0C",
     "track 1109's streaming table, zones, route and collision"},
};

constexpr std::size_t kRetailFileCount = sizeof(kRetailFiles) / sizeof(kRetailFiles[0]);

struct RetailSet {
    const char* name;
    const char* title;
    // Indices into kRetailFiles, in path order.
    std::vector<std::size_t> files;
};

const std::vector<RetailSet>& retail_sets() {
    static const std::vector<RetailSet> sets = {
        {"first-lap",
         "one timed lap of event 1 in the stock 240SX (issue #159)",
         {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13}},
        {"first-lap-1005", "event 8: DT3 - Atlantic, three forward laps",
         {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 14, 15, 16}},
        {"first-lap-1001", "event 19: a three-lap forward circuit on track 1001",
         {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 17, 18, 19}},
        {"first-lap-1002", "event 59: a four-lap reverse circuit on track 1002",
         {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 20, 21, 22}},
        {"first-lap-1004", "event 89: a three-lap forward circuit on track 1004",
         {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 23, 24, 25}},
        {"first-lap-1006", "event 122: a three-lap forward circuit on track 1006",
         {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 26, 27, 28}},
        {"first-lap-1007", "event 99: a six-lap forward circuit on track 1007",
         {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 29, 30, 31}},
        {"first-lap-1008", "event 32: a three-lap forward circuit on track 1008",
         {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 32, 33, 34}},
        {"first-lap-1301", "event 3: a four-lap forward drift race on track 1301",
         {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 35, 36, 37, 38}},
        {"first-lap-1302", "event 13: a four-lap forward drift race on track 1302",
         {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 35, 40, 41, 42}},
        {"first-lap-1303", "event 25: a four-lap forward drift race on track 1303",
         {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 35, 43, 44, 45}},
        {"first-lap-1304", "event 28: a four-lap forward drift race on track 1304",
         {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 35, 46, 47, 48}},
        {"first-lap-1305", "event 44: a four-lap forward drift race on track 1305",
         {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 39, 49, 50, 51}},
        {"first-lap-1306", "event 65: a four-lap forward drift race on track 1306",
         {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 39, 52, 53, 54}},
        {"first-lap-1307", "event 90: a four-lap forward drift race on track 1307",
         {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 39, 55, 56, 57}},
        {"first-lap-1308", "event 105: a four-lap forward drift race on track 1308",
         {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 39, 58, 59, 60}},
        {"first-lap-1201", "event 21: a forward drag race on track 1201",
         {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 35, 62, 63, 64}},
        {"first-lap-1202", "event 11: a forward drag race on track 1202",
         {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 39, 65, 66, 67}},
        {"first-lap-1206", "event 36: a forward drag race on track 1206",
         {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 35, 68, 69, 70}},
        {"first-lap-1207", "event 83: a forward drag race on track 1207",
         {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 61, 71, 72, 73}},
        {"first-lap-1210", "event 51: a forward drag race on track 1210",
         {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 35, 74, 75, 76}},
        {"first-lap-1214", "event 70: a forward drag race on track 1214",
         {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 35, 77, 78, 79}},
        {"first-lap-1102", "event 2: a forward sprint race on track 1102",
         {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 80, 81, 82}},
        {"first-lap-1103", "event 12: a forward sprint race on track 1103",
         {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 83, 84, 85}},
        {"first-lap-1104", "event 23: a forward sprint race on track 1104",
         {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 86, 87, 88}},
        {"first-lap-1105", "event 54: a forward sprint race on track 1105",
         {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 89, 90, 91}},
        {"first-lap-1106", "event 27: a forward sprint race on track 1106",
         {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 92, 93, 94}},
        {"first-lap-1107", "event 74: a forward sprint race on track 1107",
         {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 95, 96, 97}},
        {"first-lap-1108", "event 78: a forward sprint race on track 1108",
         {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 98, 99, 100}},
        {"first-lap-1109", "event 94: a forward sprint race on track 1109",
         {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 101, 102, 103}},
        {"stock-car", "the stock 240SX nfsu_switch_car draws", {0, 1, 2, 3, 4, 7}},
        {"event-1",
         "race 1 of Underground mode: event 1, track 1003, stream L1RA",
         {5, 7, 8, 10, 11, 12, 13}},
    };
    return sets;
}

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

char fold(char c) noexcept {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

bool printable(char c) noexcept {
    const unsigned char byte = static_cast<unsigned char>(c);
    return byte >= 0x20U && byte <= 0x7EU;
}

std::vector<std::string> split(const std::string& text, char separator) {
    std::vector<std::string> parts;
    std::string part;
    for (const char c : text) {
        if (c == separator) {
            parts.push_back(part);
            part.clear();
        } else {
            part += c;
        }
    }
    parts.push_back(part);
    return parts;
}

std::string hex_byte(unsigned char byte) {
    char text[8];
    std::snprintf(text, sizeof text, "0x%02X", static_cast<unsigned int>(byte));
    return text;
}

// A name from a listing or a config line, as it may appear in a sentence:
// printable ASCII as it is, anything else as \xHH, and long names cut.
std::string quoted(const std::string& text) {
    std::string out = "\"";
    std::size_t shown = 0;
    for (const char c : text) {
        if (shown == 60) {
            out += "...";
            break;
        }
        const unsigned char byte = static_cast<unsigned char>(c);
        if (printable(c) && c != '"' && c != '\\') {
            out += c;
        } else {
            char code[8];
            std::snprintf(code, sizeof code, "\\x%02X", static_cast<unsigned int>(byte));
            out += code;
        }
        ++shown;
    }
    return out + "\"";
}

std::string join_root(const std::string& root, const std::string& relative) {
    if (root.empty()) {
        return relative;
    }
    if (root.back() == '/') {
        return root + relative;
    }
    return root + "/" + relative;
}

std::string trim(const std::string& text) {
    std::size_t first = 0;
    std::size_t last = text.size();
    while (first < last && (text[first] == ' ' || text[first] == '\t')) {
        ++first;
    }
    while (last > first && (text[last - 1] == ' ' || text[last - 1] == '\t')) {
        --last;
    }
    return text.substr(first, last - first);
}

// ---------------------------------------------------------------------------
// Resolving paths, with each directory listed once
// ---------------------------------------------------------------------------

class DirectoryIndex {
public:
    DirectoryIndex(FileSystemService& files, const fs::path& root) : files_(files), root_(root) {}

    AssetResolution resolve(const std::string& relative) {
        AssetResolution result;
        fs::path current = root_;
        std::string found;
        const std::vector<std::string> names = split(relative, '/');
        for (const std::string& name : names) {
            const Listing& listing = list(found, current);
            if (!listing.listed) {
                if (found.empty()) {
                    result.lookup = AssetLookup::missing;
                    result.root_listed = false;
                    result.detail = "the root " + listing.reason;
                } else {
                    result.lookup = AssetLookup::unreadable;
                    result.detail = found + " cannot be listed: " + listing.reason;
                }
                result.found_as = found;
                return result;
            }
            std::vector<const std::string*> matches;
            for (const std::string& candidate : listing.names) {
                if (asset_names_match(candidate, name)) {
                    matches.push_back(&candidate);
                }
            }
            const std::string where = found.empty() ? std::string("the root") : found;
            if (matches.empty()) {
                result.lookup = AssetLookup::missing;
                result.detail = "no " + name + " in " + where;
                result.found_as = found;
                return result;
            }
            if (matches.size() > 1) {
                result.lookup = AssetLookup::ambiguous;
                result.detail = where + " holds " + std::to_string(matches.size()) +
                                " names that match " + name + ":";
                for (std::size_t i = 0; i < matches.size() && i < 4; ++i) {
                    result.detail += " " + quoted(*matches[i]);
                }
                result.found_as = found;
                return result;
            }
            current /= fs::path(*matches.front());
            found += (found.empty() ? "" : "/") + *matches.front();
        }
        result.lookup = AssetLookup::found;
        result.path = current;
        result.found_as = found;
        return result;
    }

    std::size_t listings() const noexcept { return listings_; }

private:
    struct Listing {
        bool listed = false;
        std::string reason;
        std::vector<std::string> names;
    };

    const Listing& list(const std::string& found, const fs::path& path) {
        const auto cached = cache_.find(found);
        if (cached != cache_.end()) {
            return cached->second;
        }
        Listing listing;
        ++listings_;
        listing.listed =
            files_.list_directory(path, kAssetMaxDirectoryNames, listing.names, listing.reason);
        if (!listing.listed) {
            listing.names.clear();
        }
        return cache_.emplace(found, std::move(listing)).first->second;
    }

    FileSystemService& files_;
    fs::path root_;
    std::map<std::string, Listing> cache_;
    std::size_t listings_ = 0;
};

// One file, found: sized, and hashed when the size is right.
void measure_file(FileSystemService& files, const fs::path& path, std::size_t index,
                  std::vector<unsigned char>& buffer, AssetFileReport& file,
                  AssetCheckReport& report, AssetCheckObserver* observer) {
    std::string reason;
    std::uint64_t size = 0;
    if (!files.file_size(path, size, reason)) {
        file.status = AssetStatus::unreadable;
        file.detail = reason;
        return;
    }
    file.sized = true;
    file.size = size;
    if (size != file.expected.size) {
        file.status = AssetStatus::wrong_size;
        file.detail = size < file.expected.size ? "shorter than the retail file: an interrupted copy?"
                                                : "longer than the retail file";
        return;
    }
    Sha256 hasher;
    std::uint64_t offset = 0;
    while (offset < size) {
        const std::uint64_t left = size - offset;
        const std::size_t want =
            left < buffer.size() ? static_cast<std::size_t>(left) : buffer.size();
        std::size_t got = 0;
        const bool read_ok = files.read_file(path, offset, buffer.data(), want, got, reason);
        ++report.reads;
        if (!read_ok) {
            file.status = AssetStatus::unreadable;
            file.detail = reason;
            return;
        }
        if (got > want) {
            got = want;
        }
        file.bytes_read += got;
        report.bytes_read += got;
        if (got == 0) {
            file.status = AssetStatus::unreadable;
            file.detail = "ended after " + std::to_string(offset) + " of " +
                          std::to_string(size) + " bytes: it changed while it was checked";
            return;
        }
        hasher.update(buffer.data(), got);
        offset += got;
        if (observer != nullptr) {
            observer->file_progress(index, offset, size);
        }
    }
    // One byte past the end must not be there: a file that grew after it was
    // sized is not the file that was hashed.
    std::size_t past = 0;
    const bool probed = files.read_file(path, size, buffer.data(), 1, past, reason);
    ++report.reads;
    if (!probed) {
        file.status = AssetStatus::unreadable;
        file.detail = reason;
        return;
    }
    if (past != 0) {
        file.bytes_read += 1;
        report.bytes_read += 1;
        file.status = AssetStatus::unreadable;
        file.detail = "holds more than the " + std::to_string(size) +
                      " bytes it was sized at: it changed while it was checked";
        return;
    }
    file.hashed = true;
    file.sha256 = sha256_hex(hasher.finish());
    if (file.sha256 == file.expected.sha256) {
        file.status = AssetStatus::ok;
        file.detail.clear();
    } else {
        file.status = AssetStatus::wrong_hash;
        file.detail = "another version of the file";
    }
}

AssetVerdict verdict_of(const AssetCheckReport& report) noexcept {
    if (report.count(AssetStatus::missing) != 0) {
        return AssetVerdict::missing_files;
    }
    if (report.count(AssetStatus::wrong_size) != 0) {
        return AssetVerdict::partial;
    }
    if (report.count(AssetStatus::wrong_hash) != 0) {
        return AssetVerdict::wrong_version;
    }
    if (report.count(AssetStatus::unreadable) != 0 || report.count(AssetStatus::ambiguous) != 0) {
        return AssetVerdict::unreadable;
    }
    return AssetVerdict::complete;
}

std::string upper(const std::string& text) {
    std::string out = text;
    for (char& c : out) {
        if (c >= 'a' && c <= 'z') {
            c = static_cast<char>(c - 'a' + 'A');
        }
    }
    return out;
}

const char* status_label(AssetStatus status) noexcept {
    switch (status) {
    case AssetStatus::ok:
        return "ok";
    case AssetStatus::missing:
        return "MISSING";
    case AssetStatus::wrong_size:
        return "WRONG SIZE";
    case AssetStatus::wrong_hash:
        return "WRONG VERSION";
    case AssetStatus::unreadable:
        return "UNREADABLE";
    case AssetStatus::ambiguous:
        return "AMBIGUOUS";
    }
    return "UNKNOWN";
}

std::string padded(const std::string& text, std::size_t width) {
    return text.size() >= width ? text + " " : text + std::string(width - text.size(), ' ');
}

// Checks a root path from a config file; see parse_asset_config.
bool normalise_console_root(const std::string& value, std::string& root, std::string& why) {
    static const std::string prefix = "sdmc:/";
    if (value.empty()) {
        why = "is empty";
        return false;
    }
    if (value.size() > kAssetRootMaxBytes) {
        why = "is " + std::to_string(value.size()) + " bytes long; at most " +
              std::to_string(kAssetRootMaxBytes) + " are allowed";
        return false;
    }
    if (value.compare(0, prefix.size(), prefix) != 0) {
        why = "must start with sdmc:/ - an absolute path on the SD card - and reads " +
              quoted(value);
        return false;
    }
    if (value.find('\\') != std::string::npos) {
        why = "holds a backslash; separate folders with /";
        return false;
    }
    std::vector<std::string> parts = split(value.substr(prefix.size()), '/');
    if (!parts.empty() && parts.back().empty()) {
        parts.pop_back();
    }
    std::string normal = prefix;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        const std::string& part = parts[i];
        if (part.empty()) {
            why = "holds an empty folder name (two / in a row)";
            return false;
        }
        if (part == "." || part == "..") {
            why = "holds the folder name " + part + ", which is not allowed";
            return false;
        }
        for (const char c : part) {
            if (!printable(c)) {
                why = std::string(c == '\t' ? "holds a tab" : "holds the byte " +
                                               hex_byte(static_cast<unsigned char>(c))) +
                      ", which a folder name on the SD card cannot";
                return false;
            }
            if (c == '"' || c == '*' || c == ':' || c == '<' || c == '>' || c == '?' ||
                c == '|') {
                why = std::string("holds the character ") + c +
                      ", which a folder name on the SD card cannot";
                return false;
            }
        }
        normal += (i == 0 ? "" : "/") + part;
    }
    root = normal;
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// Manifests
// ---------------------------------------------------------------------------

std::vector<std::string> retail_asset_manifest_names() {
    std::vector<std::string> names;
    for (const RetailSet& set : retail_sets()) {
        names.push_back(set.name);
    }
    return names;
}

bool retail_asset_manifest(const std::string& name, AssetManifest& manifest) {
    for (const RetailSet& set : retail_sets()) {
        if (name == set.name) {
            AssetManifest out;
            out.name = set.name;
            out.title = set.title;
            for (const std::size_t index : set.files) {
                if (index >= kRetailFileCount) {
                    return false;
                }
                const RetailFile& file = kRetailFiles[index];
                out.files.push_back(AssetFile{file.path, file.size, file.sha256, file.purpose});
            }
            manifest = std::move(out);
            return true;
        }
    }
    return false;
}

bool first_lap_asset_set_name(std::uint32_t track, std::string& name) {
    switch (track) {
    case 1003: name = "first-lap"; return true;
    case 1001: case 1002: case 1004: case 1005: case 1006: case 1007: case 1008:
    case 1102: case 1103: case 1104: case 1105: case 1106: case 1107:
    case 1108: case 1109:
    case 1201: case 1202: case 1206: case 1207: case 1210: case 1214:
    case 1301: case 1302: case 1303: case 1304: case 1305: case 1306:
    case 1307: case 1308:
        name = "first-lap-" + std::to_string(track); return true;
    default: return false;
    }
}

std::uint64_t asset_manifest_bytes(const AssetManifest& manifest) noexcept {
    std::uint64_t total = 0;
    for (const AssetFile& file : manifest.files) {
        total += file.size;
    }
    return total;
}

bool asset_names_match(const std::string& a, const std::string& b) noexcept {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (fold(a[i]) != fold(b[i])) {
            return false;
        }
    }
    return true;
}

bool validate_asset_manifest(const AssetManifest& manifest, std::string& reason) {
    if (manifest.name.empty()) {
        reason = "the manifest has no name";
        return false;
    }
    if (manifest.files.empty() || manifest.files.size() > kAssetMaxManifestFiles) {
        reason = "the manifest " + manifest.name + " holds " +
                 std::to_string(manifest.files.size()) + " files; it must hold 1 to " +
                 std::to_string(kAssetMaxManifestFiles);
        return false;
    }
    for (std::size_t i = 0; i < manifest.files.size(); ++i) {
        const AssetFile& file = manifest.files[i];
        const std::string which = "file " + std::to_string(i) + " " + quoted(file.path);
        if (file.path.empty() || file.path.size() > kAssetMaxPathBytes) {
            reason = which + " has a path of " + std::to_string(file.path.size()) +
                     " bytes; it must have 1 to " + std::to_string(kAssetMaxPathBytes);
            return false;
        }
        for (const char c : file.path) {
            if (!printable(c) || c == '\\' || c == ':') {
                reason = which + " holds a byte a relative path here cannot: " +
                         hex_byte(static_cast<unsigned char>(c));
                return false;
            }
        }
        for (const std::string& part : split(file.path, '/')) {
            if (part.empty() || part == "." || part == "..") {
                reason = which + " has an empty, \".\" or \"..\" part";
                return false;
            }
        }
        if (file.size > kAssetMaxFileBytes) {
            reason = which + " is " + std::to_string(file.size) + " bytes; at most " +
                     std::to_string(kAssetMaxFileBytes) + " are allowed";
            return false;
        }
        bool hex = file.sha256.size() == 64;
        for (const char c : file.sha256) {
            if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'))) {
                hex = false;
            }
        }
        if (!hex) {
            reason = which + " has a SHA-256 that is not 64 upper-case hexadecimal digits";
            return false;
        }
        for (std::size_t j = 0; j < i; ++j) {
            const std::vector<std::string> a = split(manifest.files[j].path, '/');
            const std::vector<std::string> b = split(file.path, '/');
            bool same = a.size() == b.size();
            for (std::size_t k = 0; same && k < a.size(); ++k) {
                same = asset_names_match(a[k], b[k]);
            }
            if (same) {
                reason = which + " names the same file as file " + std::to_string(j) + " " +
                         quoted(manifest.files[j].path);
                return false;
            }
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Finding and checking
// ---------------------------------------------------------------------------

AssetResolution resolve_asset_path(
    FileSystemService& files, const fs::path& root, const std::string& relative) {
    DirectoryIndex index(files, root);
    return index.resolve(relative);
}

const char* asset_status_name(AssetStatus status) noexcept {
    switch (status) {
    case AssetStatus::ok:
        return "ok";
    case AssetStatus::missing:
        return "missing";
    case AssetStatus::wrong_size:
        return "wrong-size";
    case AssetStatus::wrong_hash:
        return "wrong-hash";
    case AssetStatus::unreadable:
        return "unreadable";
    case AssetStatus::ambiguous:
        return "ambiguous";
    }
    return "unknown";
}

const char* asset_verdict_name(AssetVerdict verdict) noexcept {
    switch (verdict) {
    case AssetVerdict::complete:
        return "complete";
    case AssetVerdict::missing_files:
        return "missing files";
    case AssetVerdict::partial:
        return "partial";
    case AssetVerdict::wrong_version:
        return "wrong version";
    case AssetVerdict::unreadable:
        return "unreadable";
    }
    return "unknown";
}

std::size_t AssetCheckReport::count(AssetStatus status) const noexcept {
    const std::size_t index = static_cast<std::size_t>(status);
    return index < counts.size() ? counts[index] : 0;
}

bool check_asset_install(
    FileSystemService& files,
    const fs::path& root,
    const AssetManifest& manifest,
    AssetCheckReport& report,
    std::string& reason,
    AssetCheckObserver* observer) {
    if (!validate_asset_manifest(manifest, reason)) {
        return false;
    }
    AssetCheckReport out;
    out.manifest = manifest.name;
    out.title = manifest.title;
    out.root_listed = true;
    std::vector<unsigned char> buffer(kAssetReadChunkBytes);
    DirectoryIndex index(files, root);
    for (std::size_t i = 0; i < manifest.files.size(); ++i) {
        const AssetFile& expected = manifest.files[i];
        if (observer != nullptr) {
            observer->file_started(i, manifest.files.size(), expected);
        }
        AssetFileReport file;
        file.expected = expected;
        const AssetResolution found = index.resolve(expected.path);
        file.found = found.lookup == AssetLookup::found;
        file.found_as = found.found_as;
        if (!found.root_listed) {
            static const std::string prefix = "the root ";
            out.root_listed = false;
            out.root_problem = found.detail.compare(0, prefix.size(), prefix) == 0
                                   ? found.detail.substr(prefix.size())
                                   : found.detail;
        }
        switch (found.lookup) {
        case AssetLookup::found:
            measure_file(files, found.path, i, buffer, file, out, observer);
            break;
        case AssetLookup::missing:
            file.status = AssetStatus::missing;
            file.detail = found.detail;
            break;
        case AssetLookup::ambiguous:
            file.status = AssetStatus::ambiguous;
            file.detail = found.detail;
            break;
        case AssetLookup::unreadable:
            file.status = AssetStatus::unreadable;
            file.detail = found.detail;
            break;
        }
        ++out.counts[static_cast<std::size_t>(file.status)];
        out.files.push_back(std::move(file));
    }
    out.listings = index.listings();
    out.verdict = verdict_of(out);
    report = std::move(out);
    return true;
}

// ---------------------------------------------------------------------------
// Lines
// ---------------------------------------------------------------------------

std::vector<AssetLine> asset_report_lines(
    const AssetCheckReport& report, const std::string& root_text, AssetReportForm form) {
    const bool console = form == AssetReportForm::console;
    std::uint64_t total = 0;
    for (const AssetFileReport& file : report.files) {
        total += file.expected.size;
    }
    std::vector<AssetLine> head;
    head.push_back(AssetLine{"set " + report.manifest + ": " + report.title + ", " +
                                 std::to_string(report.files.size()) + " files, " +
                                 std::to_string(total) + " bytes",
                             AssetTone::plain});
    if (!report.root_listed) {
        head.push_back(AssetLine{"root " + root_text + " cannot be listed: it " +
                                     report.root_problem,
                                 AssetTone::bad});
    }

    std::vector<AssetLine> files;
    constexpr std::size_t kLabelWidth = 14;
    constexpr std::size_t kConsoleHashDigits = 8;
    for (const AssetFileReport& file : report.files) {
        std::string text = padded(status_label(file.status), kLabelWidth) + file.expected.path;
        switch (file.status) {
        case AssetStatus::ok:
            text += "  " + std::to_string(file.size) + " bytes, sha256 " +
                    (console ? std::string("matches") : file.sha256);
            break;
        case AssetStatus::missing:
            text += "  " + file.detail;
            break;
        case AssetStatus::wrong_size:
            text += "  " + std::to_string(file.size) + " bytes, expected " +
                    std::to_string(file.expected.size) + ": " +
                    (!console ? file.detail
                              : file.size < file.expected.size
                                    ? std::string("shorter: an interrupted copy?")
                                    : std::string("longer"));
            break;
        case AssetStatus::wrong_hash:
            text += "  " + std::to_string(file.size) + " bytes, sha256 " +
                    (console ? file.sha256.substr(0, kConsoleHashDigits) + "..." : file.sha256) +
                    " expected " +
                    (console ? file.expected.sha256.substr(0, kConsoleHashDigits) + "..."
                             : file.expected.sha256);
            break;
        case AssetStatus::unreadable:
        case AssetStatus::ambiguous:
            text += "  " + file.detail;
            break;
        }
        // Only a file that was found has a place to name; a lookup that
        // stopped part way names the folder it stopped at in its detail.
        if (file.found && file.found_as != file.expected.path) {
            text += " (found as " + file.found_as + ")";
        }
        files.push_back(AssetLine{text, file.status == AssetStatus::ok ? AssetTone::good
                                                                       : AssetTone::bad});
    }

    std::vector<AssetLine> verdict;
    const std::size_t ok = report.count(AssetStatus::ok);
    if (report.verdict == AssetVerdict::complete) {
        verdict.push_back(AssetLine{"install: COMPLETE - all " + std::to_string(ok) +
                                        " files match by name, size and SHA-256",
                                    AssetTone::good});
        verdict.push_back(AssetLine{"nothing to copy", AssetTone::good});
    } else {
        verdict.push_back(AssetLine{
            "install: " + upper(asset_verdict_name(report.verdict)) + " - " +
                std::to_string(ok) + " of " + std::to_string(report.files.size()) +
                " files ok: " + std::to_string(report.count(AssetStatus::missing)) +
                " missing, " + std::to_string(report.count(AssetStatus::wrong_size)) +
                " wrong size, " + std::to_string(report.count(AssetStatus::wrong_hash)) +
                " wrong version, " +
                std::to_string(report.count(AssetStatus::unreadable) +
                               report.count(AssetStatus::ambiguous)) +
                " unreadable",
            AssetTone::bad});
        const std::size_t copy = report.files.size() - ok;
        const std::string what = copy == 1 ? std::string("this file")
                                           : "these " + std::to_string(copy) + " files";
        verdict.push_back(AssetLine{
            "copy " + what + " from the game's installation folder" +
                (console ? " to the same " + std::string(copy == 1 ? "path" : "paths") +
                               " under " + root_text + ":"
                         : std::string(", keeping the folders:")),
            AssetTone::note});
        for (const AssetFileReport& file : report.files) {
            if (file.status != AssetStatus::ok) {
                verdict.push_back(AssetLine{
                    "  " + file.expected.path +
                        (console ? "  " + std::to_string(file.expected.size) + " bytes"
                                 : " -> " + join_root(root_text, file.expected.path)),
                    AssetTone::plain});
            }
        }
    }

    const AssetLine read{"read " + std::to_string(report.bytes_read) + " bytes in " +
                             std::to_string(report.reads) + " reads and " +
                             std::to_string(report.listings) + " directory listings",
                         AssetTone::plain};

    std::vector<AssetLine> lines = head;
    const std::vector<AssetLine>& first = console ? verdict : files;
    const std::vector<AssetLine>& second = console ? files : verdict;
    lines.insert(lines.end(), first.begin(), first.end());
    lines.insert(lines.end(), second.begin(), second.end());
    lines.push_back(read);
    return lines;
}

// ---------------------------------------------------------------------------
// The console
// ---------------------------------------------------------------------------

bool parse_asset_config(const std::string& text, std::string& root, std::string& reason) {
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEFU &&
        static_cast<unsigned char>(text[1]) == 0xBBU &&
        static_cast<unsigned char>(text[2]) == 0xBFU) {
        reason = "starts with a UTF-8 byte order mark; save it as plain ASCII";
        return false;
    }
    std::size_t line = 1;
    std::size_t column = 1;
    for (const char c : text) {
        if (c == '\n') {
            ++line;
            column = 1;
            continue;
        }
        if (!printable(c) && c != '\t' && c != '\r') {
            reason = "line " + std::to_string(line) + " column " + std::to_string(column) +
                     " holds the byte " + hex_byte(static_cast<unsigned char>(c)) +
                     ", which is not printable ASCII; the file must be plain ASCII";
            return false;
        }
        ++column;
    }
    std::vector<std::string> lines = split(text, '\n');
    std::string found;
    std::size_t found_line = 0;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        std::string content = lines[i];
        const std::string number = "line " + std::to_string(i + 1);
        if (!content.empty() && content.back() == '\r') {
            content.pop_back();
        }
        if (content.find('\r') != std::string::npos) {
            reason = number + " holds a carriage return that does not end it";
            return false;
        }
        const std::string trimmed = trim(content);
        if (trimmed.empty() || trimmed[0] == '#') {
            continue;
        }
        const std::size_t equals = trimmed.find('=');
        if (equals == std::string::npos) {
            reason = number + " is neither a comment nor a setting: " + quoted(trimmed);
            return false;
        }
        const std::string key = trim(trimmed.substr(0, equals));
        const std::string value = trim(trimmed.substr(equals + 1));
        if (key != "root") {
            reason = number + " sets " + quoted(key) +
                     ", which is not a setting; the only one is root";
            return false;
        }
        if (found_line != 0) {
            reason = number + " sets root again; line " + std::to_string(found_line) +
                     " already set it";
            return false;
        }
        std::string why;
        if (!normalise_console_root(value, found, why)) {
            reason = number + ": the root " + why;
            return false;
        }
        found_line = i + 1;
    }
    if (found_line == 0) {
        reason = "sets no root; write a line such as root = sdmc:/nfsu, or delete the "
                 "file to use the default";
        return false;
    }
    root = found;
    return true;
}

AssetConfig load_asset_config(
    FileSystemService& files,
    const std::string& sd_root,
    const std::string& relative,
    const std::string& default_root) {
    AssetConfig config;
    config.file = join_root(sd_root, relative);
    const AssetResolution found = resolve_asset_path(files, fs::path(sd_root), relative);
    if (found.lookup == AssetLookup::missing && found.root_listed) {
        config.state = AssetConfigState::absent;
        config.root = default_root;
        config.detail = found.detail;
        return config;
    }
    config.state = AssetConfigState::refused;
    config.advice = "Fix the file, or delete it to use " + default_root + ". Nothing was checked.";
    if (!found.root_listed || found.lookup == AssetLookup::unreadable) {
        // A folder the file would be in could not be listed - review comment
        // 3761, F1: the card's top level too - so the file may be there.
        static const std::string prefix = "the root ";
        config.detail = "cannot be looked for: " +
                        (found.root_listed ? found.detail
                         : found.detail.compare(0, prefix.size(), prefix) == 0
                             ? sd_root + " " + found.detail.substr(prefix.size())
                             : found.detail);
        config.advice = "Whether there is a config file cannot be told, so nothing was checked.";
        return config;
    }
    if (found.lookup != AssetLookup::found) {
        config.detail = "cannot be read: " + found.detail;
        return config;
    }
    std::string reason;
    std::uint64_t size = 0;
    if (!files.file_size(found.path, size, reason)) {
        config.detail = "cannot be read: it " + reason;
        return config;
    }
    if (size > kAssetConfigMaxBytes) {
        config.detail = "is " + std::to_string(size) + " bytes; a config file holds at most " +
                        std::to_string(kAssetConfigMaxBytes);
        return config;
    }
    std::vector<unsigned char> bytes(static_cast<std::size_t>(size) + 1);
    std::size_t got = 0;
    if (!files.read_file(found.path, 0, bytes.data(), bytes.size(), got, reason)) {
        config.detail = "cannot be read: it " + reason;
        return config;
    }
    if (got != size) {
        config.detail = "changed while it was read: " + std::to_string(got) +
                        " bytes where it was sized at " + std::to_string(size);
        return config;
    }
    const std::string text(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(got));
    std::string root;
    if (!parse_asset_config(text, root, reason)) {
        config.detail = reason;
        return config;
    }
    config.state = AssetConfigState::applied;
    config.root = root;
    return config;
}

ConsoleAssetCheck run_console_asset_check(
    FileSystemService& files,
    const ConsoleAssetSetup& setup,
    const AssetManifest& manifest,
    AssetCheckObserver* observer) {
    ConsoleAssetCheck result;
    result.config = load_asset_config(files, setup.sd_root, setup.config, setup.default_root);
    const AssetConfig& config = result.config;
    switch (config.state) {
    case AssetConfigState::absent:
        result.lines.push_back(AssetLine{"config: " + config.file + " not found; root " +
                                             config.root + " (the default)",
                                         AssetTone::note});
        break;
    case AssetConfigState::applied:
        result.lines.push_back(
            AssetLine{"config: " + config.file + " sets root " + config.root, AssetTone::note});
        break;
    case AssetConfigState::refused:
        result.lines.push_back(
            AssetLine{"config: " + config.file + " REFUSED: " + config.detail, AssetTone::bad});
        result.lines.push_back(AssetLine{config.advice, AssetTone::bad});
        result.lines.push_back(AssetLine{"set " + manifest.name + " needs these " +
                                             std::to_string(manifest.files.size()) +
                                             " files under the root:",
                                         AssetTone::note});
        for (const AssetFile& file : manifest.files) {
            result.lines.push_back(AssetLine{"  " + file.path + "  " +
                                                 std::to_string(file.size) + " bytes",
                                             AssetTone::plain});
        }
        return result;
    }
    std::string reason;
    if (!check_asset_install(files, fs::path(config.root), manifest, result.report, reason,
                             observer)) {
        result.lines.push_back(AssetLine{"nothing checked: " + reason, AssetTone::bad});
        return result;
    }
    result.checked = true;
    for (AssetLine& line :
         asset_report_lines(result.report, config.root, AssetReportForm::console)) {
        result.lines.push_back(std::move(line));
    }
    return result;
}

} // namespace nfsu
