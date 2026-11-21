// MPAudioSwitcher: the format of the audio that CAudioSwitcherFilter takes (SECURITY_AUDIT.md: SW-01).
// Transform() divides by the number of channels and by the sample rate of the input format before it looks at the type of the audio.
// CheckMediaType() decides which formats get in; the question is whether a format with 0 channels or 0 samples per second does.
//
// The constructor of the filter deletes the log file MPAudioSwitcher.log in the common application data folder of MediaPortal and the
// filter writes to it; the fixture moves the file aside and puts it back, so the test does not touch the log of an installation.
#include "stdafx.h"
#include <streams.h>
#include <ks.h>
#include <ksmedia.h>
#include <shlobj.h>
#include <gtest/gtest.h>
#include <string>
#include "AudioSwitcher.h"

void GetLogFile(TCHAR* pLog);

namespace
{
  CMediaType Pcm(WORD channels, DWORD samplesPerSec, WORD bits = 16)
  {
    CMediaType mt;
    mt.SetType(&MEDIATYPE_Audio);
    mt.SetSubtype(&MEDIASUBTYPE_PCM);
    mt.SetFormatType(&FORMAT_WaveFormatEx);
    WAVEFORMATEX* w = reinterpret_cast<WAVEFORMATEX*>(mt.AllocFormatBuffer(sizeof(WAVEFORMATEX)));
    ZeroMemory(w, sizeof(*w));
    w->wFormatTag = WAVE_FORMAT_PCM;
    w->nChannels = channels;
    w->nSamplesPerSec = samplesPerSec;
    w->wBitsPerSample = bits;
    w->nBlockAlign = static_cast<WORD>(channels * bits / 8);
    w->nAvgBytesPerSec = w->nBlockAlign * samplesPerSec;
    return mt;
  }

  class Samples
  {
  public:
    Samples(long size)
    {
      HRESULT hr = S_OK;
      m_allocator = new CMemAllocator(NAME("test allocator"), nullptr, &hr);
      m_allocator->AddRef();
      ALLOCATOR_PROPERTIES request = {}, actual = {};
      request.cBuffers = 2;
      request.cbBuffer = size;
      request.cbAlign = 1;
      m_allocator->SetProperties(&request, &actual);
      m_allocator->Commit();
    }

    ~Samples()
    {
      m_allocator->Decommit();
      m_allocator->Release();
    }

    IMediaSample* Get(long actualLength)
    {
      IMediaSample* s = nullptr;
      m_allocator->GetBuffer(&s, nullptr, nullptr, 0);
      if (s)
      {
        BYTE* data = nullptr;
        s->GetPointer(&data);
        memset(data, 0x11, s->GetSize());
        s->SetActualDataLength(actualLength);
      }
      return s;
    }

  private:
    CMemAllocator* m_allocator;
  };

  class AudioSwitcher : public ::testing::Test
  {
  protected:
    std::wstring m_log, m_aside;
    bool m_moved = false;
    CAudioSwitcherFilter* m_filter = nullptr;

    void SetUp() override
    {
      TCHAR path[MAX_PATH];
      GetLogFile(path);
      m_log = path;
      m_aside = m_log + L".testbak";
      m_moved = MoveFileExW(m_log.c_str(), m_aside.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
      HRESULT hr = S_OK;
      m_filter = new CAudioSwitcherFilter(nullptr, &hr);
      m_filter->AddRef();
    }

    void TearDown() override
    {
      m_filter->Release();
      DeleteFileW(m_log.c_str());
      if (m_moved)
        MoveFileExW(m_aside.c_str(), m_log.c_str(), MOVEFILE_REPLACE_EXISTING);
    }

    // sets the formats of the pins and runs Transform() on a sample of 'bytes' bytes
    HRESULT Transform(const CMediaType& in, const CMediaType& out, long bytes = 3840)
    {
      m_filter->GetInputPin()->CurrentMediaType() = in;
      m_filter->GetOutputPin()->CurrentMediaType() = out;
      Samples samples(8192);
      IMediaSample* pIn = samples.Get(bytes);
      IMediaSample* pOut = samples.Get(0);
      if (!pIn || !pOut)
        return E_FAIL;
      HRESULT hr = m_filter->Transform(pIn, pOut);
      pIn->Release();
      pOut->Release();
      return hr;
    }
  };
}

// The reference: an ordinary format is accepted, and a sample of it is converted.
TEST_F(AudioSwitcher, StereoFormatIsAcceptedAndConverted)
{
  CMediaType stereo = Pcm(2, 48000);
  EXPECT_EQ(m_filter->CheckMediaType(&stereo), S_OK);
  EXPECT_EQ(Transform(stereo, stereo), S_OK);
}

TEST_F(AudioSwitcher, FormatWithAnUnsupportedBitDepthIsRefused)
{
  CMediaType odd = Pcm(2, 48000, 12);
  EXPECT_NE(m_filter->CheckMediaType(&odd), S_OK);
}

// SW-01: CheckMediaType() looks at the bit depth and the tag only. Whether a format with no channels or no sample rate gets
// in decides if the divisions in Transform() can be reached.
TEST_F(AudioSwitcher, FormatWithoutChannelsIsRefused)
{
  CMediaType noChannels = Pcm(0, 48000);
  EXPECT_NE(m_filter->CheckMediaType(&noChannels), S_OK);
}

TEST_F(AudioSwitcher, FormatWithoutSampleRateIsRefused)
{
  CMediaType noRate = Pcm(2, 0);
  EXPECT_NE(m_filter->CheckMediaType(&noRate), S_OK);
}

// SW-01: and if such a format is there (a sample that arrives after a format change that the pin accepted), Transform() divides by zero.
TEST_F(AudioSwitcher, TransformDoesNotDivideByZeroForAFormatWithoutChannels)
{
  CMediaType noChannels = Pcm(0, 48000);
  CMediaType stereo = Pcm(2, 48000);
  HRESULT hr = S_OK;
  EXPECT_NO_THROW(hr = Transform(noChannels, stereo));
  EXPECT_NE(hr, S_OK);
}

TEST_F(AudioSwitcher, TransformDoesNotDivideByZeroForAFormatWithoutSampleRate)
{
  CMediaType noRate = Pcm(2, 0);
  CMediaType stereo = Pcm(2, 48000);
  HRESULT hr = S_OK;
  EXPECT_NO_THROW(hr = Transform(noRate, stereo));
  EXPECT_NE(hr, S_OK);
}
