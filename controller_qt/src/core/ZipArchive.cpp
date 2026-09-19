#include "core/ZipArchive.hpp"

#include <QDir>

#include <spdlog/spdlog.h>

#include <miniz.h>

#include "logging/Logging.hpp"

// MECHANISM ONLY (see .hpp). All install POLICY lives in
// DownloadServiceInstall.cpp — keep this file free of decisions.

namespace core {

namespace {

// miniz ships no QString support; names pass through as raw bytes.
// (m_filename is a fixed NUL-terminated char array in miniz 3.x.)
QString toQString(const char* s)
{
    return QString::fromUtf8(s);
}

} // namespace

ZipArchive::ZipArchive(const QString& zipPath)
    : m_zipPath(zipPath)
{
    load();
}

ZipArchive::~ZipArchive()
{
    if (m_reader != nullptr) {
        mz_zip_reader_end(static_cast<mz_zip_archive*>(m_reader));
        delete static_cast<mz_zip_archive*>(m_reader);
    }
}

bool ZipArchive::load()
{
    auto* reader = new mz_zip_archive{};
    if (!mz_zip_reader_init_file(reader, m_zipPath.toUtf8().constData(), 0)) {
        m_error = QStringLiteral("cannot open/parse archive (miniz status %1)")
                      .arg(static_cast<int>(mz_zip_get_last_error(reader)));
        delete reader;
        return false;
    }
    const mz_uint count = mz_zip_reader_get_num_files(reader);
    m_entries.reserve(static_cast<int>(count));
    for (mz_uint i = 0; i < count; ++i) {
        mz_zip_archive_file_stat st{};
        if (!mz_zip_reader_file_stat(reader, i, &st)) {
            m_error = QStringLiteral("central directory entry %1 unreadable").arg(i);
            mz_zip_reader_end(reader);
            delete reader;
            return false;
        }
        ZipEntry e;
        e.path = toQString(st.m_filename);
        e.isDir = (st.m_is_directory != 0) || e.path.endsWith(QLatin1Char('/'));
        e.uncompressedSize = static_cast<qint64>(st.m_uncomp_size);
        m_entries.append(e);
    }
    m_reader = reader;
    m_open = true;
    return true;
}

QByteArray ZipArchive::fileData(const QString& entryPath) const
{
    if (!m_open)
        return {};
    auto* reader = static_cast<mz_zip_archive*>(m_reader);
    const QByteArray needle = entryPath.toUtf8();
    const int index = mz_zip_reader_locate_file(reader, needle.constData(),
                                                /*pComment=*/nullptr, 0);
    if (index < 0)
        return {};
    size_t size = 0;
    void* data = mz_zip_reader_extract_to_heap(reader, static_cast<mz_uint>(index),
                                               &size, 0);
    if (data == nullptr) {
        LOG_WARN("ZipArchive: extract failed for '{}'", entryPath.toStdString());
        return {};
    }
    QByteArray out(static_cast<int>(size), Qt::Uninitialized);
    memcpy(out.data(), data, size);
    mz_free(data);
    return out;
}

} // namespace core
