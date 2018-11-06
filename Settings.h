#ifndef SETTINGS_H
#define SETTINGS_H

#include <QString>
#include <set>

/// The settigs related to a Record/Screencapture session
struct Settings
{
    enum Fmt {
        Fmt_RAW = 0,
        Fmt_PNG,
        Fmt_Mpeg2, ///< .avi, yuv420p
        Fmt_Mpeg4, ///< .avi, yuv420p
        Fmt_H264, ///< .avi, yuv420p -- uses AVCaptureSession
        Fmt_ProRes4444, ///< .mov, Apple ProRes4444 -- uses AVCaptureSession
        Fmt_ProRes422, ///< .mov, Apple ProRes422 -- uses AVCaptureSession
        Fmt_MJPEG, ///< MJPEG, motion jpeg, .mov file, yuvj420p fmt -- uses AVCaptureSession
        Fmt_FFV1, ///< .avi, lossless, bgr0 (native) fmt -- uses FFmpeg
        Fmt_LJPEG, ///< .avi, lossless, bgr0 (native) fmt
        Fmt_APNG, ///< APNG, animated PNG, rgb24 fmt
        Fmt_GIF, ///< animated GIF, rgb8 fmt -- uses FFmpeg
        Fmt_N
    };

    static const std::set<Fmt> EnabledFormats; ///< only the formats in this set are currently supported by the app.

    QString saveDir, savePrefix;
    bool zipEmbed;
    Fmt format;

    struct UART {
        QString portName;
        int baud;
        int bpsEncoded; // 4-byte value: [ 00, BITS, PARITY, STOP ] (All are QSerialPort enum vals, encoded in a DWORD)
        int flowControl; // QSerialPort::FlowControl value
    };

    UART uart;

    struct TransientNeverSavedAlwaysFromUI
    {
        void reset() { *this = TransientNeverSavedAlwaysFromUI(); }
    } transient;

    static QString fmt2String(Fmt fmt, bool prettyForUI = false);
    static Fmt string2Fmt(const QString &, bool prettyForUI = false);

    // pretty-print (with newlines) the entire settings contents to a string for debug purposes
    QString toPrettyString() const;

    Settings();
    ~Settings();

    /// other settings not recording related.  This is so we can save app settings separately without
    /// clobbering user recording settings.
    struct Other {
        int verbosity; ///< default 2 -- if 0, suppress console messages and Debug() messages from console window output
    };

    struct Appearance {
        bool useDarkStyle;
    };

    Other other;
    Appearance appearance;

    enum Scope {
        Main = 1, // everything at top-level except for stuff under .transient
        Other = 2, // everything in struct Other (.other),
        Appearance = 4, // everything in struct Appearance
        UART = 8, // everything in struct UART
        All = Main|Other|Appearance|UART
    };

    void load(int scope = All); ///< load from QSettings object
    void save(int scope = All); ///< save to QSettings object    
};

#endif // SETTINGS_H
