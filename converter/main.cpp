#include "convert.hpp"
#include <QtWidgets>
#include <QCommandLineParser>
#include <QDesktopServices>
#include <QSettings>
#include <QTextStream>
#include <QThread>
#include <functional>
#ifndef WOODY_STUDIO_VERSION
#define WOODY_STUDIO_VERSION "1.1.0 beta"
#endif

class Window final : public QMainWindow {
    bool pt = false;
    std::vector<std::function<void()>> translations;
    QLineEdit *bin, *cue, *output;
    QComboBox *language, *model, *resolution, *detail, *fps, *preset, *textures, *filter;
    QSlider *distance, *particles;
    QCheckBox *outline, *music;
    QPushButton *start, *stop, *open;
    QProgressBar *progress;
    QPlainTextEdit *log;
    QLabel *status;
    QWidget *settings;
    QThread *worker = nullptr;
    std::atomic_bool cancelled = false;
    QString completed;
    const QList<int> widths = {480, 432, 384, 360, 320, 288, 240};
    QString L(const QString &en, const QString &br) const {
        return pt ? br : en;
    }
    void text(QWidget *widget, const QString &en, const QString &br) {
        auto apply = [this, widget, en, br] {
            QString s = L(en, br);
            if (auto *l = qobject_cast<QLabel *>(widget))
                l->setText(s);
            else if (auto *b = qobject_cast<QAbstractButton *>(widget))
                b->setText(s);
            else if (auto *g = qobject_cast<QGroupBox *>(widget))
                g->setTitle(s);
        };
        translations.push_back(apply);
        apply();
    }
    QLabel *label(const QString &en, const QString &br) {
        auto *p = new QLabel;
        text(p, en, br);
        return p;
    }
    QComboBox *choices(const QStringList &en, const QStringList &br) {
        auto *c = new QComboBox;
        c->addItems(en);
        translations.push_back([=, this] {
            for (int i = 0; i < en.size(); i++)
                c->setItemText(i, pt ? br[i] : en[i]);
        });
        return c;
    }
    QSlider *slider(QFormLayout *form, const QString &en, const QString &br, int low, int high,
                    int value, const QString &suffix) {
        auto *s = new QSlider(Qt::Horizontal);
        s->setRange(low, high);
        s->setValue(value);
        auto *v = new QLabel(QString::number(value) + suffix);
        v->setMinimumWidth(42);
        auto *r = new QHBoxLayout;
        r->addWidget(s, 1);
        r->addWidget(v);
        form->addRow(label(en, br), r);
        connect(s, &QSlider::valueChanged, this,
                [=](int n) { v->setText(QString::number(n) + suffix); });
        return s;
    }
    woody::Options options() const {
        woody::Options o;
        o.bin = bin->text().trimmed();
        o.cue = cue->text().trimmed();
        o.output = output->text().trimmed();
        o.width = widths[resolution->currentIndex()];
        o.height = o.width * 272 / 480;
        o.fps = fps->currentIndex() == 0 ? 60 : fps->currentIndex() == 1 ? 40 : 30;
        o.detail = detail->currentIndex();
        o.distance = distance->value();
        o.particles = particles->value();
        o.textures = textures->currentIndex();
        o.filter = filter->currentIndex();
        o.music = music->isChecked();
        o.outline = outline->isChecked();
        o.memory = model->currentIndex() == 0 ? 0 : model->currentIndex() == 1 ? 32 : 64;
        return o;
    }
    void run() {
        auto o = options();
        if (o.bin.isEmpty() || o.cue.isEmpty() || o.output.isEmpty()) {
            status->setText(
                L("Choose BIN, CUE and output folder.", "Selecione BIN, CUE e destino."));
            return;
        }
        QSettings saved("Wendersondiazx", "WoodyPSPStudio");
        saved.setValue("bin", o.bin);
        saved.setValue("cue", o.cue);
        saved.setValue("output", o.output);
        cancelled = false;
        settings->setEnabled(false);
        language->setEnabled(false);
        start->setEnabled(false);
        stop->setEnabled(true);
        open->setEnabled(false);
        progress->setValue(0);
        log->clear();
        worker = QThread::create([this, o] {
            try {
                QString result = woody::convert(
                    o,
                    [this](int n, const QString &message) {
                        QMetaObject::invokeMethod(
                            this,
                            [this, n, message] {
                                progress->setValue(n);
                                status->setText(message);
                                log->appendPlainText(message);
                            },
                            Qt::QueuedConnection);
                    },
                    cancelled);
                QMetaObject::invokeMethod(
                    this,
                    [this, result] {
                        completed = result;
                        open->setEnabled(true);
                    },
                    Qt::QueuedConnection);
            } catch (const std::exception &e) {
                QString m = QString::fromUtf8(e.what());
                QMetaObject::invokeMethod(
                    this,
                    [this, m] {
                        status->setText(L("Conversion stopped", "Conversão interrompida"));
                        log->appendPlainText(m);
                    },
                    Qt::QueuedConnection);
            }
        });
        connect(worker, &QThread::finished, this, [this] {
            worker->deleteLater();
            worker = nullptr;
            settings->setEnabled(true);
            language->setEnabled(true);
            start->setEnabled(true);
            stop->setEnabled(false);
        });
        worker->start();
    }

  public:
    Window(const QString& languageOverride={}) {
        QSettings saved("Wendersondiazx", "WoodyPSPStudio");
        pt = (languageOverride.isEmpty()?saved.value("language", "en").toString():languageOverride) == "pt";
        setWindowTitle(QString("Woody PSP Studio %1 | Wendersondiazx").arg(WOODY_STUDIO_VERSION));
        resize(1100, 860);
        setMinimumSize(900, 700);
        auto *central = new QWidget;
        setCentralWidget(central);
        auto *root = new QVBoxLayout(central);
        root->setContentsMargins(28, 22, 28, 20);
        root->setSpacing(14);
        auto *head = new QHBoxLayout;
        auto *brand = new QLabel("WOODY <span style='color:#54dbd0'>PSP STUDIO</span>");
        brand->setObjectName("brand");
        head->addWidget(brand, 1);
        auto *version = new QLabel(QString("v%1 · runtime r7").arg(WOODY_STUDIO_VERSION));
        version->setObjectName("muted");
        head->addWidget(version);
        language = new QComboBox;
        language->addItems({"English", "Português"});
        language->setCurrentIndex(pt ? 1 : 0);
        head->addWidget(language);
        root->addLayout(head);
        auto *intro = label("Your original disc. Your PSP. One portable package.",
                            "Seu disco original. Seu PSP. Um pacote portátil.");
        intro->setObjectName("subtitle");
        root->addWidget(intro);
        settings = new QWidget;
        auto *columns = new QHBoxLayout(settings);
        columns->setContentsMargins(0, 0, 0, 0);
        columns->setSpacing(22);
        auto *source = new QGroupBox;
        text(source, "01  DISC & OUTPUT", "01  DISCO E DESTINO");
        auto *files = new QVBoxLayout(source);
        files->setSpacing(10);
        auto field = [&](const QString &en, const QString &br, const QString &ext, bool folder) {
            files->addWidget(label(en, br));
            auto *r = new QHBoxLayout;
            auto *e = new QLineEdit;
            auto *b = new QPushButton;
            text(b, "Browse", "Selecionar");
            r->addWidget(e, 1);
            r->addWidget(b);
            files->addLayout(r);
            connect(b, &QPushButton::clicked, this, [=, this] {
                QString path = folder
                                   ? QFileDialog::getExistingDirectory(this, L(en, br), e->text())
                                   : QFileDialog::getOpenFileName(this, L(en, br), e->text(), ext);
                if (!path.isEmpty())
                    e->setText(path);
            });
            return e;
        };
        bin = field("Game image (.bin)", "Imagem do jogo (.bin)", "BIN (*.bin)", false);
        cue = field("Disc descriptor (.cue)", "Descrição do disco (.cue)", "CUE (*.cue)", false);
        output = field("Output folder", "Pasta de destino", {}, true);
        bin->setText(saved.value("bin").toString());
        cue->setText(saved.value("cue").toString());
        output->setText(saved.value("output").toString());
        auto *support =
            label("Supported disc: PS2 USA · SLUS-20341\nCreates PSP/GAME/WOODYPSP with EBOOT.PBP "
                  "and data.\nExisting packages and saves are never overwritten.",
                  "Disco suportado: PS2 USA · SLUS-20341\nGera PSP/GAME/WOODYPSP com EBOOT.PBP e "
                  "data.\nPacotes e saves existentes não são sobrescritos.");
        support->setWordWrap(true);
        support->setObjectName("muted");
        files->addWidget(support);
        files->addSpacing(16);
        music = new QCheckBox;
        text(music, "Enable background music (experimental)",
             "Ativar música de fundo (experimental)");
        music->setChecked(false);
        files->addWidget(music);
        auto *audio =
            label("SOUND EFFECTS ONLY is the default. Music may still stutter on some devices. "
                  "Leave this option off to keep effects and dialogue without background music.",
                  "SOMENTE EFEITOS é o padrão. A música ainda pode travar em alguns aparelhos. "
                  "Deixe esta opção desligada para manter efeitos e diálogos sem música de fundo.");
        audio->setWordWrap(true);
        audio->setObjectName("audioNote");
        files->addWidget(audio);
        files->addStretch();
        columns->addWidget(source, 1);
        auto *profile = new QGroupBox;
        text(profile, "02  PSP RENDER PROFILE", "02  PERFIL DE RENDERIZAÇÃO PSP");
        auto *form = new QFormLayout(profile);
        form->setSpacing(10);
        model =
            choices({"Automatic · detect available RAM", "PSP-1000 · 32 MB", "PSP-2000 · 64 MB",
                     "PSP-3000 · 64 MB", "PSP Go · 64 MB", "PSP Street · 64 MB"},
                    {"Automático · detectar RAM disponível", "PSP-1000 · 32 MB", "PSP-2000 · 64 MB",
                     "PSP-3000 · 64 MB", "PSP Go · 64 MB", "PSP Street · 64 MB"});
        form->addRow(label("PSP model", "Modelo do PSP"), model);
        preset = choices({"Balanced", "Performance", "Quality"},
                         {"Equilibrado", "Desempenho", "Qualidade"});
        form->addRow(label("Preset", "Predefinição"), preset);
        resolution = new QComboBox;
        for (int w : widths)
            resolution->addItem(QString("%1 × %2").arg(w).arg(w * 272 / 480));
        form->addRow(label("Internal resolution", "Resolução interna"), resolution);
        detail = choices({"Low", "Medium", "High"}, {"Baixo", "Médio", "Alto"});
        detail->setCurrentIndex(1);
        form->addRow(label("Detail level", "Nível de detalhe"), detail);
        textures = choices({"Original textures", "Half-size world textures"},
                           {"Texturas originais", "Texturas do cenário pela metade"});
        form->addRow(label("Texture quality", "Qualidade das texturas"), textures);
        filter = choices({"Nearest · sharper pixels", "Bilinear · smoother"},
                         {"Nearest · pixels nítidos", "Bilinear · suave"});
        filter->setCurrentIndex(1);
        form->addRow(label("Texture filtering", "Filtro de texturas"), filter);
        fps = new QComboBox;
        fps->addItems({"60 FPS", "40 FPS", "30 FPS"});
        form->addRow(label("Frame-rate limit", "Limite de quadros"), fps);
        distance = slider(form, "Draw distance", "Distância visual", 40, 150, 100, "%");
        particles = slider(form, "Particle budget", "Limite de partículas", 0, 512, 128, {});
        outline = new QCheckBox;
        text(outline, "Character outline (extra GPU work)",
             "Contorno do personagem (mais custo de GPU)");
        form->addRow(outline);
        auto *limits = label(
            "Full-screen rendering. Shadows off. Frame-rate targets depend on the scene and PSP "
            "model.",
            "Renderização em tela cheia. Sem sombras. O FPS depende da cena e do modelo do PSP.");
        limits->setWordWrap(true);
        limits->setObjectName("muted");
        form->addRow(limits);
        columns->addWidget(profile, 1);
        root->addWidget(settings);
        connect(preset, &QComboBox::currentIndexChanged, this, [this](int p) {
            resolution->setCurrentIndex(p == 1 ? 3 : 0);
            detail->setCurrentIndex(p == 1 ? 0 : p == 2 ? 2 : 1);
            particles->setValue(p == 1 ? 48 : p == 2 ? 512 : 128);
            textures->setCurrentIndex(p == 1 ? 1 : 0);
            filter->setCurrentIndex(1);
            distance->setValue(p == 1 ? 70 : 100);
            fps->setCurrentIndex(p == 2 ? 1 : 0);
            outline->setChecked(false);
        });
        connect(detail, &QComboBox::currentIndexChanged, this, [this](int d) {
            particles->setValue(d == 0 ? 48 : d == 2 ? 512 : 128);
        });
        connect(model, &QComboBox::currentIndexChanged, this, [this](int m) {
            if (m == 1) {
                preset->setCurrentIndex(1);
                resolution->setCurrentIndex(3);
                textures->setCurrentIndex(1);
                fps->setCurrentIndex(1);
                particles->setValue(48);
                distance->setValue(70);
            }
        });
        connect(bin, &QLineEdit::editingFinished, this, [this] {
            if (cue->text().isEmpty()) {
                auto f = QFileInfo(bin->text());
                QString c = f.absolutePath() + '/' + f.completeBaseName() + ".cue";
                if (QFileInfo::exists(c))
                    cue->setText(c);
            }
        });
        auto *actions = new QHBoxLayout;
        status = label("Ready to convert your original disc",
                       "Pronto para converter seu disco original");
        actions->addWidget(status, 1);
        stop = new QPushButton;
        text(stop, "Cancel", "Cancelar");
        stop->setEnabled(false);
        actions->addWidget(stop);
        start = new QPushButton;
        text(start, "BUILD PSP PACKAGE", "GERAR PACOTE PSP");
        start->setObjectName("primary");
        actions->addWidget(start);
        root->addLayout(actions);
        progress = new QProgressBar;
        progress->setValue(0);
        root->addWidget(progress);
        log = new QPlainTextEdit;
        log->setReadOnly(true);
        log->setMaximumBlockCount(1500);
        log->setMinimumHeight(60);
        root->addWidget(log, 1);
        auto *footer = new QHBoxLayout;
        auto *link =
            new QLabel("<a href='https://github.com/wendersondiazx' "
                       "style='color:#54dbd0;text-decoration:none'>github.com/wendersondiazx</a>");
        link->setOpenExternalLinks(true);
        footer->addWidget(link, 1);
        open = new QPushButton;
        text(open, "Open generated package", "Abrir pacote gerado");
        open->setEnabled(false);
        footer->addWidget(open);
        root->addLayout(footer);
        setStyleSheet(
            "QWidget{background:#101822;color:#e5edf7;font:13px 'Segoe UI';} "
            "QLabel,QCheckBox{background:transparent;} "
            "QLabel#brand{font-size:30px;font-weight:800;letter-spacing:1px;} "
            "QLabel#subtitle{color:#a8b9ce;font-size:14px;} QLabel#muted{color:#91a6bd;} "
            "QLabel#audioNote{color:#e6c38e;padding:12px;background:#252529;border-radius:8px;} "
            "QGroupBox{background:#172330;border:0;border-radius:12px;margin-top:15px;padding:22px "
            "16px 16px;} QGroupBox::title{left:16px;color:#54dbd0;subcontrol-origin:margin;} "
            "QLineEdit,QComboBox,QPlainTextEdit{background:#0c131d;border:1px solid "
            "#2b3b4f;border-radius:6px;padding:8px;selection-background-color:#235c66;} "
            "QLineEdit:focus,QComboBox:focus{border-color:#54dbd0;} "
            "QPushButton{background:#26374b;border:0;border-radius:6px;padding:10px "
            "16px;font-weight:600;} QPushButton:hover{background:#36516b;} "
            "QPushButton:disabled{background:#1a2533;color:#61758c;} "
            "QPushButton#primary{background:#df4154;color:white;padding:13px 20px;} "
            "QPushButton#primary:hover{background:#f45767;} "
            "QProgressBar{background:#26374b;border:0;border-radius:4px;text-align:center;height:"
            "12px;} QProgressBar::chunk{background:#54dbd0;border-radius:4px;} "
            "QSlider::groove:horizontal{height:4px;background:#354b62;} "
            "QSlider::handle:horizontal{background:#54dbd0;width:13px;margin:-5px "
            "0;border-radius:6px;}");
        connect(language, &QComboBox::currentIndexChanged, this, [this](int i) {
            pt = i == 1;
            QSettings s("Wendersondiazx", "WoodyPSPStudio");
            s.setValue("language", pt ? "pt" : "en");
            for (auto &f : translations)
                f();
        });
        for (auto &f : translations)
            f();
        connect(start, &QPushButton::clicked, this, [this] { run(); });
        connect(stop, &QPushButton::clicked, this, [this] {
            cancelled = true;
            stop->setEnabled(false);
        });
        connect(open, &QPushButton::clicked, this,
                [this] { QDesktopServices::openUrl(QUrl::fromLocalFile(completed)); });
    }
    void closeEvent(QCloseEvent *e) override {
        if (worker) {
            cancelled = true;
            e->ignore();
        } else
            e->accept();
    }
};
int main(int argc, char **argv) {
    bool cli = false;
    for (int i = 1; i < argc; i++)
        if (QString::fromLocal8Bit(argv[i]) == "--convert" ||
            QString::fromLocal8Bit(argv[i]) == "--self-test")
            cli = true;
    if (cli) {
        QCoreApplication app(argc, argv);
        QCommandLineParser p;
        p.addHelpOption();
        p.addOptions({{"convert", "Convert original BIN+CUE"},
                      {"self-test", "Test converter"},
                      {"bin", "BIN image", "path"},
                      {"cue", "CUE descriptor", "path"},
                      {"output", "Output folder", "path"},
                      {"width", "Internal width", "pixels", "480"},
                      {"fps", "FPS limit", "fps", "60"},
                      {"detail", "Detail: 0,1,2", "level", "1"},
                      {"distance", "Draw distance percent", "percent", "100"},
                      {"memory", "Memory: 0(auto),32,64", "MB", "0"},
                      {"particles", "Particle budget", "count", "128"},
                      {"textures", "Textures: 0(original),1(half)", "quality", "0"},
                      {"filter", "Filter: 0(nearest),1(bilinear)", "mode", "1"},
                      {"music", "Enable experimental background music"}});
        p.process(app);
        try {
            if (p.isSet("self-test")) {
                woody::selfTest();
                return 0;
            }
            woody::Options o;
            o.bin = p.value("bin");
            o.cue = p.value("cue");
            o.output = p.value("output");
            o.width = p.value("width").toInt();
            o.height = o.width * 272 / 480;
            o.fps = p.value("fps").toInt();
            o.detail = p.value("detail").toInt();
            o.distance = p.value("distance").toInt();
            o.memory = p.value("memory").toInt();
            o.particles = p.value("particles").toInt();
            o.textures = p.value("textures").toInt();
            o.filter = p.value("filter").toInt();
            o.music = p.isSet("music");
            std::atomic_bool stop = false;
            woody::convert(
                o,
                [](int n, const QString &m) { QTextStream(stdout) << n << "% " << m << Qt::endl; },
                stop);
            return 0;
        } catch (const std::exception &e) {
            QTextStream(stderr) << e.what() << Qt::endl;
            return 1;
        }
    }
    QApplication app(argc, argv);
    app.setStyle("Fusion");
    Window w;
    w.show();
    for (int i = 1; i + 1 < argc; i++)
        if (QString::fromLocal8Bit(argv[i]) == "--preview") {
            QString path = QString::fromLocal8Bit(argv[i + 1]);
            QTimer::singleShot(300, &w,
                               [&app, &w, path] { app.exit(w.grab().save(path) ? 0 : 1); });
        }
    return app.exec();
}
