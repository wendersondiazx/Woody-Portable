#include "convert.hpp"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QTemporaryDir>
#include <QSaveFile>
#include <QRegularExpression>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QMap>
#include <QtEndian>
#include <algorithm>
#include <stdexcept>
#include <vector>

namespace woody {
[[noreturn]] static void fail(const QString &s) {
    throw std::runtime_error(s.toStdString());
}
static void require(bool ok, const QString &s) {
    if (!ok)
        fail(s);
}
static quint32 le(const QByteArray &b, qsizetype p) {
    require(p >= 0 && p <= b.size() - 4, "Arquivo truncado (inteiro).");
    return qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(b.constData() + p));
}
static quint16 le16(const QByteArray &b, qsizetype p) {
    require(p >= 0 && p <= b.size() - 2, "Arquivo truncado (inteiro curto).");
    return qFromLittleEndian<quint16>(reinterpret_cast<const uchar *>(b.constData() + p));
}
static quint32 be(const QByteArray &b, qsizetype p) {
    require(p >= 0 && p <= b.size() - 4, "Cabeçalho VAG truncado.");
    return qFromBigEndian<quint32>(reinterpret_cast<const uchar *>(b.constData() + p));
}
static void u32(QByteArray &b, quint32 v) {
    char raw[4];
    qToLittleEndian(v, reinterpret_cast<uchar *>(raw));
    b.append(raw, 4);
}
static void u16(QByteArray &b, quint16 v) {
    char raw[2];
    qToLittleEndian(v, reinterpret_cast<uchar *>(raw));
    b.append(raw, 2);
}
static QByteArray part(const QByteArray &b, qsizetype p, qsizetype n) {
    require(p >= 0 && n >= 0 && p <= b.size() && n <= b.size() - p,
            "Recurso truncado ou tamanho inválido.");
    return b.mid(p, n);
}
static QByteArray hash(const QByteArray &b) {
    return QCryptographicHash::hash(b, QCryptographicHash::Sha256).toHex();
}
static void write(const QString &path, const QByteArray &data) {
    require(QDir().mkpath(QFileInfo(path).absolutePath()),
            "Não foi possível criar a pasta de saída.");
    QSaveFile file(path);
    require(file.open(QIODevice::WriteOnly), file.errorString());
    require(file.write(data) == data.size() && file.commit(), "Falha ao gravar " + path);
}
struct Record {
    QString path;
    quint32 lba, size;
};
class Disc {
    QFile file;
    quint64 first = 0;
    QSet<quint32> visited;

  public:
    std::vector<Record> records;
    Disc(const Options &o) : file(o.bin) {
        QFile cue(o.cue);
        require(cue.open(QIODevice::ReadOnly), "Não foi possível abrir o CUE.");
        QString text = QString::fromUtf8(cue.readAll());
        QRegularExpression re("^\\s*FILE\\s+(?:\"([^\"]+)\"|(\\S+))\\s+BINARY\\s*$",
                              QRegularExpression::MultilineOption |
                                  QRegularExpression::CaseInsensitiveOption);
        auto matches = re.globalMatch(text);
        require(matches.hasNext(), "CUE sem FILE BINARY válido.");
        auto m = matches.next();
        require(!matches.hasNext(), "Selecione um CUE com um único BIN.");
        QString filename = m.captured(1).isEmpty() ? m.captured(2) : m.captured(1);
        QString declared =
            QFileInfo(QDir(QFileInfo(o.cue).absolutePath()).filePath(filename)).canonicalFilePath();
        require(!declared.isEmpty() && declared == QFileInfo(o.bin).canonicalFilePath(),
                "O BIN selecionado não corresponde ao FILE do CUE.");
        QRegularExpression track("^\\s*TRACK\\s+01\\s+MODE2/2352\\s*$",
                                 QRegularExpression::MultilineOption |
                                     QRegularExpression::CaseInsensitiveOption);
        require(track.match(text).hasMatch(), "Esta edição requer TRACK 01 MODE2/2352.");
        QRegularExpression index("^\\s*INDEX\\s+01\\s+(\\d+):(\\d+):(\\d+)\\s*$",
                                 QRegularExpression::MultilineOption |
                                     QRegularExpression::CaseInsensitiveOption);
        auto ix = index.match(text);
        require(ix.hasMatch(), "CUE sem INDEX 01.");
        int sec = ix.captured(2).toInt(), frame = ix.captured(3).toInt();
        require(sec < 60 && frame < 75, "Tempo inválido no CUE.");
        first = (quint64(ix.captured(1).toUInt()) * 60 + sec) * 75 + frame;
        require(file.open(QIODevice::ReadOnly), "Não foi possível abrir o BIN.");
        require(file.size() % 2352 == 0, "Tamanho do BIN não corresponde a setores de 2352 bytes.");
        auto pvd = read(16, 2048);
        require(pvd.left(7) == QByteArray("\1CD001\1", 7), "Não é uma imagem ISO9660 Mode2/Form1.");
        directory(le(pvd, 158), le(pvd, 166), {}, 0);
        auto boot = std::find_if(records.begin(), records.end(),
                                 [](const Record &r) { return r.path == "SYSTEM.CNF"; });
        require(boot != records.end() && read(boot->lba, boot->size).contains("SLUS_203.41"),
                "Edição não suportada. Use Woody Woodpecker PS2 USA (SLUS-20341).");
    }
    QByteArray read(quint32 lba, quint32 size) {
        require(size <= 512u * 1024u * 1024u, "Recurso maior que o limite de segurança.");
        quint64 start = (quint64(lba) + first) * 2352, sectors = (quint64(size) + 2047) / 2048;
        require(start <= quint64(file.size()) && sectors * 2352 <= quint64(file.size()) - start,
                "Setor fora do BIN.");
        require(file.seek(qint64(start)), "Falha ao buscar setor do BIN.");
        QByteArray out;
        out.reserve(size);
        while (size) {
            auto raw = file.read(2352);
            require(raw.size() == 2352, "Setor truncado.");
            require(raw.left(12) == QByteArray::fromHex("00ffffffffffffffffffff00") &&
                        uchar(raw[15]) == 2 && !(uchar(raw[18]) & 0x20),
                    "Setor não é Mode2/Form1.");
            quint32 n = std::min(size, 2048u);
            out.append(raw.constData() + 24, n);
            size -= n;
        }
        return out;
    }
    void directory(quint32 lba, quint32 size, const QString &parent, int depth) {
        require(depth <= 12 && size <= 16 * 1024 * 1024 && !visited.contains(lba),
                "Árvore ISO inválida ou cíclica.");
        visited.insert(lba);
        auto data = read(lba, size);
        qsizetype pos = 0;
        while (pos < data.size()) {
            int n = uchar(data[pos]);
            if (!n) {
                pos = (pos / 2048 + 1) * 2048;
                continue;
            }
            auto entry = part(data, pos, n);
            pos += n;
            require(n >= 34, "Registro ISO inválido.");
            auto name = part(entry, 33, uchar(entry[32]));
            if (name == QByteArray(1, '\0') || name == QByteArray(1, '\1'))
                continue;
            QString s = QString::fromLatin1(name).section(';', 0, 0);
            require(!s.isEmpty() && s != "." && s != ".." &&
                        !s.contains(QRegularExpression("[/\\\\:\\x00-\\x1f]")),
                    "Nome inseguro no ISO.");
            QString path = parent.isEmpty() ? s : parent + '/' + s;
            quint32 at = le(entry, 2), bytes = le(entry, 10);
            require(!(uchar(entry[25]) & 0x80), "ISO multi-extent não suportado.");
            if (uchar(entry[25]) & 2)
                directory(at, bytes, path, depth + 1);
            else {
                require(records.size() < 10000, "Arquivos demais no ISO.");
                records.push_back({path, at, bytes});
            }
        }
    }
};
static QByteArray sound(const QByteArray &payload) {
    auto data = part(payload, 16, payload.size() - 16);
    require(data.left(4) == "VAGp" && data.size() >= 48, "Som RCK sem VAG válido.");
    auto size = be(data, 12), rate = be(data, 16);
    require(size % 16 == 0 && size == data.size() - 48 && rate > 0 && rate <= 48000,
            "Formato VAG não suportado.");
    const int ca[5] = {0, 60, 115, 98, 122}, cb[5] = {0, 0, -52, -55, -60};
    int h1 = 0, h2 = 0;
    QByteArray pcm;
    for (qsizetype p = 48; p < data.size(); p += 16) {
        unsigned pred = uchar(data[p]) >> 4, shift = uchar(data[p]) & 15,
                 flags = uchar(data[p + 1]);
        require(pred < 5 && shift <= 12, "Preditor VAG inválido.");
        if (flags == 7)
            break;
        for (int j = 0; j < 28; j++) {
            int nib = (uchar(data[p + 2 + j / 2]) >> (4 * (j % 2))) & 15;
            if (nib >= 8)
                nib -= 16;
            int sample =
                std::clamp(((nib * 4096) >> shift) + ((h1 * ca[pred] + h2 * cb[pred] + 32) >> 6),
                           -32768, 32767);
            u16(pcm, quint16(sample));
            h2 = h1;
            h1 = sample;
        }
        if (flags & 1)
            break;
    }
    QByteArray result;
    u32(result, quint32(pcm.size()));
    u32(result, rate);
    u32(result, 16);
    u32(result, 1);
    return result + pcm;
}
static QByteArray image(const QByteArray &p) {
    quint16 w = le16(p, 0), h = le16(p, 2), bpp = le16(p, 4), alpha = le16(p, 6);
    require(w > 0 && h > 0 && w <= 2048 && h <= 2048, "Dimensão RCK inválida.");
    qsizetype pixels = qsizetype(w) * h;
    QByteArray rgba;
    if (bpp == 8) {
        require(p.size() == 8 + 1024 + pixels, "Imagem indexada truncada.");
        rgba.resize(pixels * 4);
        for (qsizetype i = 0; i < pixels; i++)
            memcpy(rgba.data() + i * 4, p.constData() + 8 + uchar(p[1032 + i]) * 4, 4);
    } else {
        require((bpp == 24 || bpp == 32) && p.size() == 8 + pixels * 4,
                "Imagem RCK não suportada.");
        rgba = p.mid(8);
    }
    for (qsizetype i = 3; i < rgba.size(); i += 4)
        rgba[i] = char(alpha ? std::min(255, int(uchar(rgba[i])) * 2) : 255);
    QByteArray out;
    u16(out, w);
    u16(out, h);
    u16(out, 32);
    u16(out, alpha);
    return out + rgba;
}
static QByteArray bank(const QByteArray &data) {
    require(data.size() >= 56, "Cabeçalho RCK truncado.");
    qsizetype offset = 56;
    QByteArray out = data.left(56);
    for (int kind = 0; kind < 4; kind++) {
        quint32 count = le(data, 32 + kind * 4);
        require(count <= 100000, "Contagem RCK inválida.");
        for (quint32 i = 0; i < count; i++) {
            quint32 size = le(data, offset), reserved = le(data, offset + 4);
            offset += 8;
            auto p = part(data, offset, size);
            offset += size;
            if (kind == 0)
                p = sound(p);
            else if (kind == 1)
                p = image(p);
            else if (kind == 3) {
                quint64 glyphs = le(p, 0), pages = le(p, 4), side = le(p, 8),
                        base = 28 + glyphs * 20;
                require(side <= 2048 && pages <= 16 && base <= quint64(p.size()) &&
                            quint64(p.size()) - base == pages * side * side,
                        "Fonte PS2 inválida.");
                QByteArray expanded = p.left(qsizetype(base));
                expanded.reserve(qsizetype(base + pages * side * side * 4));
                for (qsizetype a = qsizetype(base); a < p.size(); a++) {
                    expanded.append("\xff\xff\xff", 3);
                    expanded.append(char(std::min(255, int(uchar(p[a])) * 2)));
                }
                p = expanded;
            }
            u32(out, quint32(p.size()));
            u32(out, reserved);
            out += p;
        }
    }
    require(offset == data.size(), "Dados extras inesperados no RCK.");
    return out;
}
static QString destination(const QString &path) {
    auto p = path.split('/');
    require(!p.isEmpty(), "Caminho de recurso vazio.");
    if (p[0] == "COMMON") {
        const QMap<QString, QString> names = {{"LAYER1.RCK", "Woody.rck"},
                                              {"LAYER2.RCK", "Knothead.rck"},
                                              {"LAYER3.RCK", "Splinter.rck"}};
        require(names.contains(p.last()), "Banco COMMON desconhecido.");
        return "Common/" + names[p.last()];
    }
    if (p.size() == 1 || p[0] == "MUSICS" || p[0] == "GAME" || p[0] == "VIDEO")
        return path;
    QString level = p[p.size() - 2];
    const QMap<QString, QString> aliases = {{"WSW", "WWS"}, {"WSK", "KWS"}, {"WSS", "SWS"}};
    if (aliases.contains(level))
        level = aliases[level];
    QString filename =
        p.last() == "CODE." ? "code" : level + '.' + QFileInfo(p.last()).suffix().toLower();
    return "Data/" + level + '/' + filename;
}
QString convert(const Options &o, const Progress &progress, const std::atomic_bool &cancel) {
    require(!o.output.trimmed().isEmpty(), "Selecione uma pasta de saída.");
    require(o.width >= 240 && o.width <= 480 && o.width % 8 == 0 && o.height == o.width * 272 / 480,
            "Resolução inválida.");
    require((o.fps == 30 || o.fps == 40 || o.fps == 60) && o.detail >= 0 && o.detail <= 2 &&
                o.distance >= 40 && o.distance <= 150,
            "Configuração de desempenho inválida.");
    require((o.memory == 0 || o.memory == 32 || o.memory == 64) && o.particles >= 0 &&
                o.particles <= 512 && o.textures >= 0 && o.textures <= 1 && o.filter >= 0 &&
                o.filter <= 1,
            "Invalid render profile.");
    progress(0, "Checking BIN+CUE and game edition...");
    Disc disc(o);
    const QString parent = QDir(o.output).absoluteFilePath("PSP/GAME");
    require(QDir().mkpath(parent), "Pasta de saída não pode ser criada.");
    const QString final = parent + "/WOODYPSP";
    require(
        !QFileInfo::exists(final),
        "A pasta WOODYPSP já existe. Escolha outra pasta de saída para preservar seus arquivos.");
    QTemporaryDir stage(parent + "/.woody-conversion-XXXXXX");
    require(stage.isValid(), "Falha ao criar a área temporária.");
    QString game = stage.path() + "/WOODYPSP";
    QDir().mkpath(game);
    QJsonArray manifest;
    QSet<QString> outputs;
    std::vector<Record> assets;
    for (auto &r : disc.records)
        if (r.path.startsWith("WDATA/"))
            assets.push_back(r);
    require(!assets.empty(), "WDATA não encontrado no disco.");
    size_t done = 0;
    quint64 total = 0;
    for (const auto &r : assets) {
        require(!cancel.load(), "Conversão cancelada; nenhum pacote parcial foi publicado.");
        const QString relative = r.path.mid(6), dest = destination(relative);
        require(!outputs.contains(dest.toLower()), "Arquivos de saída duplicados no disco.");
        outputs.insert(dest.toLower());
        progress(int(done * 90 / assets.size()), "Converting " + relative);
        auto original = disc.read(r.lba, r.size);
        auto result = relative.endsWith(".RCK", Qt::CaseInsensitive) ? bank(original) : original;
        write(game + "/data/" + dest, result);
        total += quint64(result.size());
        manifest.append(QJsonObject{{"source", relative},
                                    {"source_sha256", QString::fromLatin1(hash(original))},
                                    {"output", dest},
                                    {"output_sha256", QString::fromLatin1(hash(result))}});
        ++done;
    }
    require(outputs.contains("data/house/house.ins") && outputs.contains("common/woody.rck"),
            "Recursos essenciais do jogo estão ausentes.");
    write(game + "/data/assets-manifest.json", QJsonDocument(manifest).toJson());
    progress(94, "Writing EBOOT, presentation and engine settings...");
    for (const auto &pair : std::vector<std::pair<QString, QString>>{
             {":/runtime/EBOOT.PBP", "EBOOT.PBP"},
             {":/presentation/cover.rgba", "data/presentation/cover.rgba"},
             {":/legal/LICENSE", "LICENSE"},
             {":/legal/NOTICE", "NOTICE"}}) {
        QFile f(pair.first);
        if (pair.first == ":/presentation/cover.rgba" && !f.exists())
            continue;
        require(f.open(QIODevice::ReadOnly), "Recurso do conversor ausente: " + pair.first);
        write(game + '/' + pair.second, f.readAll());
    }
    write(game + "/woody-fps.txt", QByteArray::number(o.fps) + "\n");
    for (const QString &name : QDir(":/loading").entryList({"loading_*.rgba"}, QDir::Files)) {
        QFile background(":/loading/" + name);
        require(background.open(QIODevice::ReadOnly), "Missing loading background: " + name);
        write(game + "/data/presentation/" + name, background.readAll());
    }
    write(game + "/woody-performance.cfg",
          QString("width=%1\nheight=%2\ndetail=%3\ndistance=%4\nmemory=%5\nparticles=%6\ntextures=%"
                  "7\nfilter=%8\nmusic=%9\n")
              .arg(o.width)
              .arg(o.height)
              .arg(o.detail)
              .arg(o.distance)
              .arg(o.memory)
              .arg(o.particles)
              .arg(o.textures)
              .arg(o.filter)
              .arg(o.music ? 1 : 0)
              .toUtf8());
    write(game + "/woody-outline.txt", o.outline ? "1\n" : "0\n");
    QJsonObject report{{"game_id", "SLUS-20341"},
                       {"converter", "C++ / Qt"},
                       {"version", "1.1.0 beta"},
                       {"runtime", "r8"},
                       {"engine_language", "C++17"},
                       {"memory_mb", o.memory},
                       {"particles", o.particles},
                       {"half_textures", o.textures == 1},
                       {"music_enabled", o.music},
                       {"assets", int(done)},
                       {"data_bytes", qint64(total)},
                       {"target_fps", o.fps},
                       {"width", o.width},
                       {"height", o.height},
                       {"detail", o.detail},
                       {"distance_percent", o.distance},
                       {"hardware_fps_verified", false}};
    write(game + "/conversion.json", QJsonDocument(report).toJson());
    write(game + "/LEIA-ME.txt",
          "WoodyPSP r8 / Studio 1.1.0 beta\r\nCopy WOODYPSP to PSP/GAME on your PSP storage.\r\n"
          "Profiles support PSP-1000, 2000, 3000, Go and Street (32/64 MB).\r\n"
          "Music is experimental and disabled by default. Sound effects and dialogue remain "
          "enabled.\r\n"
          "Frame rate depends on the level and "
          "hardware.\r\nhttps://github.com/wendersondiazx/Woody-Portable\r\n");
    require(!cancel.load(), "Conversão cancelada.");
    require(QDir().rename(game, final), "Não foi possível publicar a pasta final.");
    progress(100, "Ready: " + final);
    return final;
}
void selfTest() {
    QByteArray data(56, '\0');
    require(bank(data) == data, "Falha no banco vazio.");
    QByteArray p;
    u16(p, 1);
    u16(p, 1);
    u16(p, 8);
    u16(p, 1);
    p += QByteArray(1024, '\0');
    p[8] = 1;
    p[9] = 2;
    p[10] = 3;
    p[11] = 64;
    p.append('\0');
    auto b = image(p);
    require(b.size() == 12 && le16(b, 4) == 32 && uchar(b[11]) == 128 && b[8] == 1,
            "Falha na expansão de imagem e alpha.");
    bool rejected = false;
    try {
        bank(QByteArray(10, '\0'));
    } catch (const std::exception &) {
        rejected = true;
    }
    require(rejected, "Banco truncado aceito.");
    require(destination("WOODY/W1A/CODE.") == "Data/W1A/code" &&
                destination("COMMON/LAYER1.RCK") == "Common/Woody.rck",
            "Falha no mapeamento de recursos.");
}
} // namespace woody
