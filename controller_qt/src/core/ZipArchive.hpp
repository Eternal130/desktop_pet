#pragma once

// ZipArchive (P5, §B.4) — the SINGLE place zip mechanics are touched.
//
// Replaceability contract: callers depend only on this API. The design doc
// named Qt's private QZipReader; the Qt distributions this tree builds
// against do not ship private headers (verified — no qzipreader_p.h), so
// the backing store is miniz (FetchContent'd, same isolation guarantee).
// Swapping the backing store (QZipReader, libarchive, Minizip-ng) is a
// rewrite of ZipArchive.cpp ALONE — no other file includes zip internals.
//
// This layer is MECHANISM only (list entries, read bytes). The install
// POLICY (zip-slip normalization, allow-listed targets, sentinel checks,
// atomic writes, final move) lives in DownloadServiceInstall.cpp.

#include <QByteArray>
#include <QString>
#include <QVector>

namespace core {

struct ZipEntry
{
    QString path;        // stored name, exactly as the archive records it
    bool isDir = false;
    qint64 uncompressedSize = 0;
};

class ZipArchive
{
public:
    explicit ZipArchive(const QString& zipPath);
    ~ZipArchive();

    ZipArchive(const ZipArchive&) = delete;
    ZipArchive& operator=(const ZipArchive&) = delete;

    // True when the archive opened and its central directory parsed.
    bool isOpen() const { return m_open; }
    QString errorString() const { return m_error; }

    QVector<ZipEntry> entries() const { return m_entries; }

    // Decompressed content of one file entry; empty QByteArray when the
    // entry is missing or decompression failed (never throws).
    QByteArray fileData(const QString& entryPath) const;

private:
    bool load();

    void* m_reader = nullptr; // miniz mz_zip_archive* (opaque — private dep)
    QString m_zipPath;
    QString m_error;
    bool m_open = false;
    QVector<ZipEntry> m_entries;
};

} // namespace core
