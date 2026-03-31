#include "acceltool/utils/csv_writer.h"

#include <stdexcept>

namespace acceltool
{
    CsvWriter::~CsvWriter()
    {
        flush();
    }

    void CsvWriter::open(const std::string& path)
    {
        m_out.open(path, std::ios::out | std::ios::trunc);
        if (!m_out.is_open())
        {
            throw std::runtime_error("Failed to open CSV file: " + path);
        }
    }

    void CsvWriter::writeHeader()
    {
        m_out << "sample_index,"
             "node_address,"
             "timestamp_sec,"
             "x,"
             "y,"
             "z,"
             "magnitude_xy,"
             "magnitude_xyz,"
             "applied_spec,"
             "exceeds_spec\n";
    }

    void CsvWriter::writeRow(const ProcessedSample& sample)
    {
        m_out
        << sample.sampleIndex << ','
        << sample.nodeAddress << ','
        << sample.timestampSeconds << ','
        << sample.x << ','
        << sample.y << ','
        << sample.z << ','
        << sample.magnitudeXY << ','
        << sample.magnitudeXYZ << ','
        << sample.appliedSpec << ','
        << (sample.exceedsSpec ? 1 : 0) << '\n';
    }

    void CsvWriter::flush()
    {
        if (m_out.is_open())
        {
            m_out.flush();
        }
    }

    bool CsvWriter::isOpen() const noexcept
    {
        return m_out.is_open();
    }
}
