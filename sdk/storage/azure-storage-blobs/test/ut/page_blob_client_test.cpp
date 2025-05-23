// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "page_blob_client_test.hpp"

#include <azure/core/cryptography/hash.hpp>
#include <azure/storage/blobs/blob_lease_client.hpp>
#include <azure/storage/common/crypt.hpp>
#include <azure/storage/common/internal/file_io.hpp>
#include <azure/storage/files/shares.hpp>

#include <future>
#include <vector>

namespace Azure { namespace Storage { namespace Blobs { namespace Models {

  bool operator==(const BlobHttpHeaders& lhs, const BlobHttpHeaders& rhs);

}}}} // namespace Azure::Storage::Blobs::Models

namespace Azure { namespace Storage { namespace Test {

  void PageBlobClientTest::SetUp()
  {
    BlobContainerClientTest::SetUp();
    if (shouldSkipTest())
    {
      return;
    }
    m_blobName = RandomString();
    m_pageBlobClient = std::make_shared<Blobs::PageBlobClient>(
        m_blobContainerClient->GetPageBlobClient(m_blobName));
    m_blobContent = RandomBuffer(static_cast<size_t>(1_KB));
    auto blobContent
        = Azure::Core::IO::MemoryBodyStream(m_blobContent.data(), m_blobContent.size());
    m_pageBlobClient->Create(2_KB);
    m_pageBlobClient->UploadPages(0, blobContent);
    m_blobContent.resize(static_cast<size_t>(2_KB));
  }

  TEST_F(PageBlobClientTest, Constructors_LIVEONLY_)
  {
    auto clientOptions = InitStorageClientOptions<Blobs::BlobClientOptions>();
    {
      auto pageBlobClient = Blobs::PageBlobClient::CreateFromConnectionString(
          StandardStorageConnectionString(), m_containerName, m_blobName, clientOptions);
      EXPECT_NO_THROW(pageBlobClient.GetProperties());
    }

    {
      auto cred = _internal::ParseConnectionString(StandardStorageConnectionString()).KeyCredential;
      auto pageBlobClient = Blobs::PageBlobClient(m_pageBlobClient->GetUrl(), cred, clientOptions);
      EXPECT_NO_THROW(pageBlobClient.GetProperties());
    }

    {
      auto pageBlobClient
          = Blobs::PageBlobClient(m_pageBlobClient->GetUrl() + GetSas(), clientOptions);
      EXPECT_NO_THROW(pageBlobClient.GetProperties());
    }
  }

  TEST_F(PageBlobClientTest, WithSnapshotVersionId)
  {
    const std::string timestamp1 = "2001-01-01T01:01:01.1111000Z";
    const std::string timestamp2 = "2022-02-02T02:02:02.2222000Z";

    auto client1 = m_pageBlobClient->WithSnapshot(timestamp1);
    EXPECT_FALSE(client1.GetUrl().find("snapshot=" + timestamp1) == std::string::npos);
    EXPECT_TRUE(client1.GetUrl().find("snapshot=" + timestamp2) == std::string::npos);
    client1 = client1.WithSnapshot(timestamp2);
    EXPECT_TRUE(client1.GetUrl().find("snapshot=" + timestamp1) == std::string::npos);
    EXPECT_FALSE(client1.GetUrl().find("snapshot=" + timestamp2) == std::string::npos);
    client1 = client1.WithSnapshot("");
    EXPECT_TRUE(client1.GetUrl().find("snapshot=" + timestamp1) == std::string::npos);
    EXPECT_TRUE(client1.GetUrl().find("snapshot=" + timestamp2) == std::string::npos);

    client1 = m_pageBlobClient->WithVersionId(timestamp1);
    EXPECT_FALSE(client1.GetUrl().find("versionid=" + timestamp1) == std::string::npos);
    EXPECT_TRUE(client1.GetUrl().find("versionid=" + timestamp2) == std::string::npos);
    client1 = client1.WithVersionId(timestamp2);
    EXPECT_TRUE(client1.GetUrl().find("versionid=" + timestamp1) == std::string::npos);
    EXPECT_FALSE(client1.GetUrl().find("versionid=" + timestamp2) == std::string::npos);
    client1 = client1.WithVersionId("");
    EXPECT_TRUE(client1.GetUrl().find("versionid=" + timestamp1) == std::string::npos);
    EXPECT_TRUE(client1.GetUrl().find("versionid=" + timestamp2) == std::string::npos);
  }

  TEST_F(PageBlobClientTest, CreateDelete)
  {
    auto pageBlobClient = *m_pageBlobClient;

    Blobs::CreatePageBlobOptions createOptions;
    createOptions.HttpHeaders.ContentType = "application/x-binary";
    createOptions.HttpHeaders.ContentLanguage = "en-US";
    createOptions.HttpHeaders.ContentDisposition = "attachment";
    createOptions.HttpHeaders.CacheControl = "no-cache";
    createOptions.HttpHeaders.ContentEncoding = "identify";
    createOptions.Metadata = RandomMetadata();
    createOptions.Tags["key1"] = "value1";
    createOptions.Tags["key2"] = "value2";
    createOptions.Tags["key3 +-./:=_"] = "v1 +-./:=_";
    auto blobContentInfo = pageBlobClient.Create(0, createOptions);
    EXPECT_TRUE(blobContentInfo.Value.ETag.HasValue());
    EXPECT_TRUE(IsValidTime(blobContentInfo.Value.LastModified));
    EXPECT_TRUE(blobContentInfo.Value.VersionId.HasValue());
    EXPECT_FALSE(blobContentInfo.Value.VersionId.Value().empty());
    EXPECT_FALSE(blobContentInfo.Value.EncryptionScope.HasValue());
    EXPECT_FALSE(blobContentInfo.Value.EncryptionKeySha256.HasValue());

    auto properties = pageBlobClient.GetProperties().Value;
    EXPECT_EQ(properties.Metadata, createOptions.Metadata);
    EXPECT_EQ(properties.HttpHeaders, createOptions.HttpHeaders);
    EXPECT_EQ(pageBlobClient.GetTags().Value, createOptions.Tags);

    pageBlobClient.Delete();
    EXPECT_THROW(pageBlobClient.Delete(), StorageException);
  }

  TEST_F(PageBlobClientTest, Resize)
  {
    auto pageBlobClient = *m_pageBlobClient;

    pageBlobClient.Create(0);

    EXPECT_EQ(pageBlobClient.GetProperties().Value.BlobSize, 0);
    pageBlobClient.Resize(static_cast<int64_t>(2_KB));
    EXPECT_EQ(static_cast<uint64_t>(pageBlobClient.GetProperties().Value.BlobSize), 2_KB);
    pageBlobClient.Resize(static_cast<int64_t>(1_KB));
    EXPECT_EQ(static_cast<uint64_t>(pageBlobClient.GetProperties().Value.BlobSize), 1_KB);
  }

  TEST_F(PageBlobClientTest, UploadClear)
  {
    auto pageBlobClient = *m_pageBlobClient;

    std::vector<uint8_t> blobContent = RandomBuffer(static_cast<size_t>(4_KB));

    pageBlobClient.Create(8_KB);
    auto pageContent = Azure::Core::IO::MemoryBodyStream(blobContent.data(), blobContent.size());
    pageBlobClient.UploadPages(2_KB, pageContent);
    // |_|_|x|x|  |x|x|_|_|
    blobContent.insert(blobContent.begin(), static_cast<size_t>(2_KB), '\x00');
    blobContent.resize(static_cast<size_t>(8_KB), '\x00');
    pageBlobClient.ClearPages({2_KB, 1_KB});
    // |_|_|_|x|  |x|x|_|_|
    std::fill(
        blobContent.begin() + static_cast<size_t>(2_KB),
        blobContent.begin() + static_cast<size_t>(2_KB + 1_KB),
        '\x00');

    auto downloadContent = pageBlobClient.Download();
    EXPECT_EQ(ReadBodyStream(downloadContent.Value.BodyStream), blobContent);

    std::vector<Core::Http::HttpRange> pageRanges;
    for (auto pageResult = pageBlobClient.GetPageRanges(); pageResult.HasPage();
         pageResult.MoveToNextPage())
    {
      pageRanges.insert(
          pageRanges.end(), pageResult.PageRanges.begin(), pageResult.PageRanges.end());
    }
    ASSERT_FALSE(pageRanges.empty());
    EXPECT_EQ(static_cast<uint64_t>(pageRanges[0].Offset), 3_KB);
    EXPECT_EQ(static_cast<uint64_t>(pageRanges[0].Length.Value()), 3_KB);

    Azure::Storage::Blobs::GetPageRangesOptions options;
    options.Range = Core::Http::HttpRange();
    options.Range.Value().Offset = 4_KB;
    options.Range.Value().Length = 1_KB;
    pageRanges.clear();
    for (auto pageResult = pageBlobClient.GetPageRanges(options); pageResult.HasPage();
         pageResult.MoveToNextPage())
    {
      pageRanges.insert(
          pageRanges.end(), pageResult.PageRanges.begin(), pageResult.PageRanges.end());
    }
    ASSERT_FALSE(pageRanges.empty());
    EXPECT_EQ(static_cast<uint64_t>(pageRanges[0].Offset), 4_KB);
    EXPECT_EQ(static_cast<uint64_t>(pageRanges[0].Length.Value()), 1_KB);

    auto snapshot = pageBlobClient.CreateSnapshot().Value.Snapshot;
    // |_|_|_|x|  |x|x|_|_| This is what's in snapshot
    blobContent.resize(static_cast<size_t>(1_KB));
    auto pageClient = Azure::Core::IO::MemoryBodyStream(blobContent.data(), blobContent.size());
    pageBlobClient.UploadPages(0, pageClient);
    pageBlobClient.ClearPages({3_KB, 1_KB});
    // |x|_|_|_|  |x|x|_|_|

    pageRanges.clear();
    std::vector<Core::Http::HttpRange> clearRanges;
    for (auto pageResult = pageBlobClient.GetPageRangesDiff(snapshot); pageResult.HasPage();
         pageResult.MoveToNextPage())
    {
      pageRanges.insert(
          pageRanges.end(), pageResult.PageRanges.begin(), pageResult.PageRanges.end());
      clearRanges.insert(
          clearRanges.end(), pageResult.ClearRanges.begin(), pageResult.ClearRanges.end());
    }
    ASSERT_FALSE(pageRanges.empty());
    ASSERT_FALSE(clearRanges.empty());
    EXPECT_EQ(pageRanges[0].Offset, 0);
    EXPECT_EQ(static_cast<uint64_t>(pageRanges[0].Length.Value()), 1_KB);
    EXPECT_EQ(static_cast<uint64_t>(clearRanges[0].Offset), 3_KB);
    EXPECT_EQ(static_cast<uint64_t>(clearRanges[0].Length.Value()), 1_KB);
  }

  TEST_F(PageBlobClientTest, GetPageRangesContinuation)
  {
    auto pageBlobClient = *m_pageBlobClient;

    std::vector<uint8_t> blobContent = RandomBuffer(512);

    pageBlobClient.Create(8_KB);
    auto pageContent = Azure::Core::IO::MemoryBodyStream(blobContent.data(), blobContent.size());
    pageBlobClient.UploadPages(0, pageContent);
    pageContent.Rewind();
    pageBlobClient.UploadPages(1024, pageContent);
    pageContent.Rewind();
    pageBlobClient.UploadPages(4096, pageContent);

    Blobs::GetPageRangesOptions options;
    options.PageSizeHint = 1;
    size_t numRanges = 0;
    for (auto pageResult = pageBlobClient.GetPageRanges(options); pageResult.HasPage();
         pageResult.MoveToNextPage())
    {
      EXPECT_EQ(pageResult.PageRanges.size(), static_cast<size_t>(1));
      numRanges += pageResult.PageRanges.size();
    }
    EXPECT_EQ(numRanges, static_cast<size_t>(3));
  }

  TEST_F(PageBlobClientTest, GetPageRangesDiffContinuation)
  {
    auto pageBlobClient = *m_pageBlobClient;
    std::vector<uint8_t> blobContent = RandomBuffer(512);

    pageBlobClient.Create(8_KB);
    auto snapshot = pageBlobClient.CreateSnapshot().Value.Snapshot;

    for (int i = 0; i < 3; ++i)
    {
      auto pageContent = Azure::Core::IO::MemoryBodyStream(blobContent.data(), blobContent.size());
      pageBlobClient.UploadPages(1024 * i, pageContent);
    }

    Blobs::GetPageRangesOptions options;
    options.PageSizeHint = 1;
    int numPages = 0;
    int numItems = 0;
    for (auto page = pageBlobClient.GetPageRangesDiff(snapshot, options); page.HasPage();
         page.MoveToNextPage())
    {
      ++numPages;
      numItems += static_cast<int>(page.PageRanges.size() + page.ClearRanges.size());
    }
    EXPECT_GT(numPages, 2);
    EXPECT_EQ(numItems, 3);

    // range
    numItems = 0;
    options.Range = Core::Http::HttpRange();
    options.Range.Value().Offset = 1024 * 2;
    for (auto page = pageBlobClient.GetPageRangesDiff(snapshot, options); page.HasPage();
         page.MoveToNextPage())
    {
      numItems += static_cast<int>(page.PageRanges.size() + page.ClearRanges.size());
    }
    EXPECT_EQ(numItems, 1);
  }

  TEST_F(PageBlobClientTest, UploadFromUri)
  {
    auto pageBlobClient = *m_pageBlobClient;

    auto pageBlobClient2 = GetPageBlobClientTestForTest(RandomString());
    pageBlobClient2.Create(m_blobContent.size());
    pageBlobClient2.UploadPagesFromUri(
        0, pageBlobClient.GetUrl() + GetSas(), {0, static_cast<int64_t>(m_blobContent.size())});
    EXPECT_EQ(
        pageBlobClient2.Download().Value.BodyStream->ReadToEnd(),
        pageBlobClient.Download().Value.BodyStream->ReadToEnd());
  }

  TEST_F(PageBlobClientTest, OAuthUploadFromUri)
  {
    auto pageBlobClient = *m_pageBlobClient;

    auto pageBlobClient2 = GetPageBlobClientTestForTest(RandomString());
    pageBlobClient2.Create(m_blobContent.size());

    Azure::Core::Credentials::TokenRequestContext requestContext;
    requestContext.Scopes = {Storage::_internal::StorageScope};
    auto oauthToken = GetTestCredential()->GetToken(requestContext, Azure::Core::Context());

    Storage::Blobs::UploadPagesFromUriOptions options;
    options.SourceAuthorization = "Bearer " + oauthToken.Token;
    pageBlobClient2.UploadPagesFromUri(
        0, pageBlobClient.GetUrl(), {0, static_cast<int64_t>(m_blobContent.size())}, options);
    EXPECT_EQ(
        pageBlobClient2.Download().Value.BodyStream->ReadToEnd(),
        pageBlobClient.Download().Value.BodyStream->ReadToEnd());
  }

  TEST_F(PageBlobClientTest, OAuthUploadFromUri_SourceFileShare_PLAYBACKONLY_)
  {
    auto shareClientOptions = InitStorageClientOptions<Files::Shares::ShareClientOptions>();
    shareClientOptions.ShareTokenIntent = Files::Shares::Models::ShareTokenIntent::Backup;
    auto oauthCredential = GetTestCredential();
    auto shareServiceClient = Files::Shares::ShareServiceClient::CreateFromConnectionString(
        StandardStorageConnectionString(), shareClientOptions);
    shareServiceClient = Files::Shares::ShareServiceClient(
        shareServiceClient.GetUrl(), oauthCredential, shareClientOptions);
    auto shareClient = shareServiceClient.GetShareClient(LowercaseRandomString());
    shareClient.Create();

    size_t fileSize = 1 * 1024;
    std::string fileName = RandomString() + "file";
    std::vector<uint8_t> fileContent = RandomBuffer(fileSize);
    auto memBodyStream = Core::IO::MemoryBodyStream(fileContent);
    auto sourceFileClient = shareClient.GetRootDirectoryClient().GetFileClient(fileName);
    sourceFileClient.Create(fileSize);
    EXPECT_NO_THROW(sourceFileClient.UploadRange(0, memBodyStream));

    auto destBlobClient = GetPageBlobClientTestForTest(RandomString());
    destBlobClient.Create(fileSize);

    Azure::Core::Credentials::TokenRequestContext requestContext;
    requestContext.Scopes = {Storage::_internal::StorageScope};
    auto oauthToken = oauthCredential->GetToken(requestContext, Azure::Core::Context());

    Storage::Blobs::UploadPagesFromUriOptions options;
    options.SourceAuthorization = "Bearer " + oauthToken.Token;
    options.FileRequestIntent = Blobs::Models::FileShareTokenIntent::Backup;
    EXPECT_NO_THROW(destBlobClient.UploadPagesFromUri(
        0, sourceFileClient.GetUrl(), {0, static_cast<int64_t>(fileSize)}, options));
    EXPECT_EQ(destBlobClient.Download().Value.BodyStream->ReadToEnd(), fileContent);

    EXPECT_NO_THROW(shareClient.DeleteIfExists());
  }

  TEST_F(PageBlobClientTest, StartCopyIncremental_LIVEONLY_)
  {
    auto pageBlobClient = *m_pageBlobClient;

    const std::string blobName = RandomString();
    auto pageBlobClient2 = GetPageBlobClientTestForTest(blobName);

    std::string snapshot = pageBlobClient.CreateSnapshot().Value.Snapshot;
    Azure::Core::Url sourceUri(pageBlobClient.WithSnapshot(snapshot).GetUrl());
    auto copyInfo
        = pageBlobClient2.StartCopyIncremental(AppendQueryParameters(sourceUri, GetSas()));
    EXPECT_EQ(
        copyInfo.GetRawResponse().GetStatusCode(), Azure::Core::Http::HttpStatusCode::Accepted);
    auto getPropertiesResult = copyInfo.PollUntilDone(PollInterval());
    ASSERT_TRUE(getPropertiesResult.Value.CopyStatus.HasValue());
    EXPECT_EQ(getPropertiesResult.Value.CopyStatus.Value(), Blobs::Models::CopyStatus::Success);
    ASSERT_TRUE(getPropertiesResult.Value.CopyId.HasValue());
    EXPECT_FALSE(getPropertiesResult.Value.CopyId.Value().empty());
    ASSERT_TRUE(getPropertiesResult.Value.CopySource.HasValue());
    EXPECT_FALSE(getPropertiesResult.Value.CopySource.Value().empty());
    ASSERT_TRUE(getPropertiesResult.Value.IsIncrementalCopy.HasValue());
    EXPECT_TRUE(getPropertiesResult.Value.IsIncrementalCopy.Value());
    ASSERT_TRUE(getPropertiesResult.Value.IncrementalCopyDestinationSnapshot.HasValue());
    EXPECT_FALSE(getPropertiesResult.Value.IncrementalCopyDestinationSnapshot.Value().empty());
    ASSERT_TRUE(getPropertiesResult.Value.CopyCompletedOn.HasValue());
    EXPECT_TRUE(IsValidTime(getPropertiesResult.Value.CopyCompletedOn.Value()));
    ASSERT_TRUE(getPropertiesResult.Value.CopyProgress.HasValue());
    EXPECT_FALSE(getPropertiesResult.Value.CopyProgress.Value().empty());

    auto blobItem = GetBlobItem(blobName, Blobs::Models::ListBlobsIncludeFlags::Copy);
    ASSERT_TRUE(blobItem.Details.IsIncrementalCopy.HasValue());
    EXPECT_TRUE(blobItem.Details.IsIncrementalCopy.Value());
    ASSERT_TRUE(blobItem.Details.IncrementalCopyDestinationSnapshot.HasValue());
    EXPECT_FALSE(blobItem.Details.IncrementalCopyDestinationSnapshot.Value().empty());
  }

  TEST_F(PageBlobClientTest, Lease)
  {
    auto pageBlobClient = *m_pageBlobClient;

    {
      std::string leaseId1 = RandomUUID();
      auto leaseDuration = std::chrono::seconds(20);
      Blobs::BlobLeaseClient leaseClient(pageBlobClient, leaseId1);
      auto aLease = leaseClient.Acquire(leaseDuration).Value;
      EXPECT_TRUE(aLease.ETag.HasValue());
      EXPECT_TRUE(IsValidTime(aLease.LastModified));
      EXPECT_EQ(aLease.LeaseId, leaseId1);
      EXPECT_EQ(leaseClient.GetLeaseId(), leaseId1);
      aLease = leaseClient.Acquire(leaseDuration).Value;
      EXPECT_TRUE(aLease.ETag.HasValue());
      EXPECT_TRUE(IsValidTime(aLease.LastModified));
      EXPECT_EQ(aLease.LeaseId, leaseId1);

      auto properties = pageBlobClient.GetProperties().Value;
      EXPECT_EQ(properties.LeaseState.Value(), Blobs::Models::LeaseState::Leased);
      EXPECT_EQ(properties.LeaseStatus.Value(), Blobs::Models::LeaseStatus::Locked);
      EXPECT_EQ(properties.LeaseDuration.Value(), Blobs::Models::LeaseDurationType::Fixed);

      auto rLease = leaseClient.Renew().Value;
      EXPECT_TRUE(rLease.ETag.HasValue());
      EXPECT_TRUE(IsValidTime(rLease.LastModified));
      EXPECT_EQ(rLease.LeaseId, leaseId1);

      std::string leaseId2 = RandomUUID();
      EXPECT_NE(leaseId1, leaseId2);
      auto cLease = leaseClient.Change(leaseId2).Value;
      EXPECT_TRUE(cLease.ETag.HasValue());
      EXPECT_TRUE(IsValidTime(cLease.LastModified));
      EXPECT_EQ(cLease.LeaseId, leaseId2);
      EXPECT_EQ(leaseClient.GetLeaseId(), leaseId2);

      auto blobInfo = leaseClient.Release().Value;
      EXPECT_TRUE(blobInfo.ETag.HasValue());
      EXPECT_TRUE(IsValidTime(blobInfo.LastModified));
    }

    {
      Blobs::BlobLeaseClient leaseClient(pageBlobClient, RandomUUID());
      auto aLease = leaseClient.Acquire(Blobs::BlobLeaseClient::InfiniteLeaseDuration).Value;
      auto properties = pageBlobClient.GetProperties().Value;
      EXPECT_EQ(properties.LeaseDuration.Value(), Blobs::Models::LeaseDurationType::Infinite);
      auto brokenLease = leaseClient.Break().Value;
      EXPECT_TRUE(brokenLease.ETag.HasValue());
      EXPECT_TRUE(IsValidTime(brokenLease.LastModified));
    }

    {
      Blobs::BlobLeaseClient leaseClient(pageBlobClient, RandomUUID());
      auto leaseDuration = std::chrono::seconds(20);
      auto aLease = leaseClient.Acquire(leaseDuration).Value;
      auto brokenLease = leaseClient.Break().Value;
      EXPECT_TRUE(brokenLease.ETag.HasValue());
      EXPECT_TRUE(IsValidTime(brokenLease.LastModified));

      Blobs::BreakLeaseOptions options;
      options.BreakPeriod = std::chrono::seconds(0);
      leaseClient.Break(options);
    }
  }

  TEST_F(PageBlobClientTest, ContentHash)
  {
    auto pageBlobClient = *m_pageBlobClient;

    std::vector<uint8_t> blobContent = RandomBuffer(static_cast<size_t>(4_KB));
    const std::vector<uint8_t> contentMd5
        = Azure::Core::Cryptography::Md5Hash().Final(blobContent.data(), blobContent.size());
    const std::vector<uint8_t> contentCrc64
        = Azure::Storage::Crc64Hash().Final(blobContent.data(), blobContent.size());

    pageBlobClient.Create(blobContent.size());
    auto contentStream = Azure::Core::IO::MemoryBodyStream(blobContent.data(), blobContent.size());
    pageBlobClient.UploadPages(0, contentStream);

    auto pageBlobClient2 = GetPageBlobClientTestForTest(RandomString());
    pageBlobClient2.Create(blobContent.size());

    Blobs::UploadPagesOptions options1;
    options1.TransactionalContentHash = ContentHash();
    options1.TransactionalContentHash.Value().Algorithm = HashAlgorithm::Md5;
    options1.TransactionalContentHash.Value().Value = Azure::Core::Convert::Base64Decode(DummyMd5);
    contentStream.Rewind();
    EXPECT_THROW(pageBlobClient2.UploadPages(0, contentStream, options1), StorageException);
    options1.TransactionalContentHash.Value().Value = contentMd5;
    contentStream.Rewind();
    EXPECT_NO_THROW(pageBlobClient2.UploadPages(0, contentStream, options1));
    options1.TransactionalContentHash.Value().Algorithm = HashAlgorithm::Crc64;
    options1.TransactionalContentHash.Value().Value
        = Azure::Core::Convert::Base64Decode(DummyCrc64);
    contentStream.Rewind();
    EXPECT_THROW(pageBlobClient2.UploadPages(0, contentStream, options1), StorageException);
    options1.TransactionalContentHash.Value().Value = contentCrc64;
    contentStream.Rewind();
    EXPECT_NO_THROW(pageBlobClient2.UploadPages(0, contentStream, options1));

    Blobs::UploadPagesFromUriOptions options2;
    Azure::Core::Http::HttpRange sourceRange;
    sourceRange.Offset = 0;
    sourceRange.Length = blobContent.size();
    options2.TransactionalContentHash = ContentHash();
    options2.TransactionalContentHash.Value().Algorithm = HashAlgorithm::Md5;
    options2.TransactionalContentHash.Value().Value = Azure::Core::Convert::Base64Decode(DummyMd5);
    EXPECT_THROW(
        pageBlobClient2.UploadPagesFromUri(
            0, pageBlobClient.GetUrl() + GetSas(), sourceRange, options2),
        StorageException);
    options2.TransactionalContentHash.Value().Value = contentMd5;
    EXPECT_NO_THROW(pageBlobClient2.UploadPagesFromUri(
        0, pageBlobClient.GetUrl() + GetSas(), sourceRange, options2));
  }

  TEST_F(PageBlobClientTest, DISABLED_UploadPagesFromUriCrc64AccessCondition)
  {
    auto pageBlobClient = *m_pageBlobClient;

    std::vector<uint8_t> blobContent = RandomBuffer(static_cast<size_t>(4_KB));
    const std::vector<uint8_t> contentCrc64
        = Azure::Storage::Crc64Hash().Final(blobContent.data(), blobContent.size());

    pageBlobClient.Create(blobContent.size());
    auto contentStream = Azure::Core::IO::MemoryBodyStream(blobContent.data(), blobContent.size());
    pageBlobClient.UploadPages(0, contentStream);

    auto pageBlobClient2 = GetPageBlobClientTestForTest(RandomString());
    pageBlobClient2.Create(blobContent.size());

    Blobs::UploadPagesFromUriOptions options;
    Azure::Core::Http::HttpRange sourceRange;
    sourceRange.Offset = 0;
    sourceRange.Length = blobContent.size();
    options.TransactionalContentHash = ContentHash();
    options.TransactionalContentHash.Value().Algorithm = HashAlgorithm::Crc64;
    options.TransactionalContentHash.Value().Value = Azure::Core::Convert::Base64Decode(DummyCrc64);
    EXPECT_THROW(
        pageBlobClient2.UploadPagesFromUri(
            0, pageBlobClient.GetUrl() + GetSas(), sourceRange, options),
        StorageException);
    options.TransactionalContentHash.Value().Value = contentCrc64;
    EXPECT_NO_THROW(pageBlobClient2.UploadPagesFromUri(
        0, pageBlobClient.GetUrl() + GetSas(), sourceRange, options));
  }

  TEST_F(PageBlobClientTest, CreateIfNotExists)
  {
    auto pageBlobClient = GetPageBlobClientTestForTest(RandomString());

    auto blobClientWithoutAuth = Azure::Storage::Blobs::PageBlobClient(
        pageBlobClient.GetUrl(),
        InitStorageClientOptions<Azure::Storage::Blobs::BlobClientOptions>());
    EXPECT_THROW(blobClientWithoutAuth.CreateIfNotExists(m_blobContent.size()), StorageException);
    {
      auto response = pageBlobClient.CreateIfNotExists(m_blobContent.size());
      EXPECT_TRUE(response.Value.Created);
    }

    auto blobContent
        = Azure::Core::IO::MemoryBodyStream(m_blobContent.data(), m_blobContent.size());
    pageBlobClient.UploadPages(0, blobContent);
    {
      auto response = pageBlobClient.CreateIfNotExists(m_blobContent.size());
      EXPECT_FALSE(response.Value.Created);
    }
    auto downloadStream = std::move(pageBlobClient.Download().Value.BodyStream);
    EXPECT_EQ(downloadStream->ReadToEnd(Azure::Core::Context()), m_blobContent);
  }

  TEST_F(PageBlobClientTest, SourceBlobAccessConditions)
  {
    auto sourceBlobClient = GetPageBlobClientTestForTest("source" + RandomString());

    const std::string url = sourceBlobClient.GetUrl() + GetSas();

    const int64_t blobSize = 512;
    auto createResponse = sourceBlobClient.Create(blobSize);
    Azure::ETag eTag = createResponse.Value.ETag;
    auto lastModifiedTime = createResponse.Value.LastModified;
    auto timeBeforeStr = lastModifiedTime - std::chrono::seconds(1);
    auto timeAfterStr = lastModifiedTime + std::chrono::seconds(1);

    auto destBlobClient = GetPageBlobClientTestForTest("dest" + RandomString());
    destBlobClient.Create(blobSize);

    {
      Blobs::UploadPagesFromUriOptions options;
      options.SourceAccessConditions.IfMatch = eTag;
      EXPECT_NO_THROW(destBlobClient.UploadPagesFromUri(0, url, {0, blobSize}, options));
      options.SourceAccessConditions.IfMatch = DummyETag;
      EXPECT_THROW(
          destBlobClient.UploadPagesFromUri(0, url, {0, blobSize}, options), StorageException);
    }
    {
      Blobs::UploadPagesFromUriOptions options;
      options.SourceAccessConditions.IfNoneMatch = DummyETag;
      EXPECT_NO_THROW(destBlobClient.UploadPagesFromUri(0, url, {0, blobSize}, options));
      options.SourceAccessConditions.IfNoneMatch = eTag;
      EXPECT_THROW(
          destBlobClient.UploadPagesFromUri(0, url, {0, blobSize}, options), StorageException);
    }
    {
      Blobs::UploadPagesFromUriOptions options;
      options.SourceAccessConditions.IfModifiedSince = timeBeforeStr;
      EXPECT_NO_THROW(destBlobClient.UploadPagesFromUri(0, url, {0, blobSize}, options));
      options.SourceAccessConditions.IfModifiedSince = timeAfterStr;
      EXPECT_THROW(
          destBlobClient.UploadPagesFromUri(0, url, {0, blobSize}, options), StorageException);
    }
    {
      Blobs::UploadPagesFromUriOptions options;
      options.SourceAccessConditions.IfUnmodifiedSince = timeAfterStr;
      EXPECT_NO_THROW(destBlobClient.UploadPagesFromUri(0, url, {0, blobSize}, options));
      options.SourceAccessConditions.IfUnmodifiedSince = timeBeforeStr;
      EXPECT_THROW(
          destBlobClient.UploadPagesFromUri(0, url, {0, blobSize}, options), StorageException);
    }
  }

  TEST_F(PageBlobClientTest, UpdateSequenceNumber)
  {
    auto blobClient = *m_pageBlobClient;

    blobClient.Create(512);

    Blobs::Models::BlobHttpHeaders headers;
    headers.ContentType = "text/plain";
    blobClient.SetHttpHeaders(headers);

    Blobs::UpdatePageBlobSequenceNumberOptions options;
    options.SequenceNumber = 100;
    auto res
        = blobClient.UpdateSequenceNumber(Blobs::Models::SequenceNumberAction::Update, options);
    EXPECT_TRUE(res.Value.ETag.HasValue());
    EXPECT_TRUE(IsValidTime(res.Value.LastModified));
    EXPECT_EQ(res.Value.SequenceNumber, 100);
    EXPECT_EQ(blobClient.GetProperties().Value.SequenceNumber.Value(), 100);

    options.SequenceNumber = 200;
    res = blobClient.UpdateSequenceNumber(Blobs::Models::SequenceNumberAction::Update, options);
    EXPECT_EQ(res.Value.SequenceNumber, 200);
    EXPECT_EQ(blobClient.GetProperties().Value.SequenceNumber.Value(), 200);

    options.SequenceNumber = 50;
    res = blobClient.UpdateSequenceNumber(Blobs::Models::SequenceNumberAction::Max, options);
    EXPECT_EQ(res.Value.SequenceNumber, 200);
    EXPECT_EQ(blobClient.GetProperties().Value.SequenceNumber.Value(), 200);
    options.SequenceNumber = 300;
    res = blobClient.UpdateSequenceNumber(Blobs::Models::SequenceNumberAction::Max, options);
    EXPECT_EQ(res.Value.SequenceNumber, 300);
    EXPECT_EQ(blobClient.GetProperties().Value.SequenceNumber.Value(), 300);

    options.SequenceNumber.Reset();
    res = blobClient.UpdateSequenceNumber(Blobs::Models::SequenceNumberAction::Increment, options);
    EXPECT_EQ(res.Value.SequenceNumber, 301);
    EXPECT_EQ(blobClient.GetProperties().Value.SequenceNumber.Value(), 301);

    EXPECT_EQ(blobClient.GetProperties().Value.HttpHeaders.ContentType, headers.ContentType);
  }

  TEST_F(PageBlobClientTest, PageBlobAccessConditions)
  {
    auto blobClient = *m_pageBlobClient;

    blobClient.Create(1024);
    Blobs::UpdatePageBlobSequenceNumberOptions updateSequenceNumberOptions;
    updateSequenceNumberOptions.SequenceNumber = 100;
    blobClient.UpdateSequenceNumber(
        Blobs::Models::SequenceNumberAction::Update, updateSequenceNumberOptions);

    enum class AccessConditionType
    {
      Eq,
      Lt,
      LtOrEq,
    };
    enum class Operation
    {
      Upload,
      UploadFromUri,
      Clear,
    };
    for (auto o : {Operation::Upload, Operation::UploadFromUri, Operation::Clear})
    {

      for (auto willSuccess : {true, false})
      {
        for (auto t :
             {AccessConditionType::Eq, AccessConditionType::Lt, AccessConditionType::LtOrEq})
        {
          Blobs::PageBlobAccessConditions accessConditions;
          if (t == AccessConditionType::Eq)
          {
            accessConditions.IfSequenceNumberEqual
                = blobClient.GetProperties().Value.SequenceNumber.Value();
            if (!willSuccess)
            {
              accessConditions.IfSequenceNumberEqual.Value()++;
            }
          }
          else if (t == AccessConditionType::Lt)
          {
            accessConditions.IfSequenceNumberLessThan
                = blobClient.GetProperties().Value.SequenceNumber.Value();
            if (willSuccess)
            {
              accessConditions.IfSequenceNumberLessThan.Value()++;
            }
          }
          else if (t == AccessConditionType::LtOrEq)
          {
            accessConditions.IfSequenceNumberLessThanOrEqual
                = blobClient.GetProperties().Value.SequenceNumber.Value();
            if (!willSuccess)
            {
              accessConditions.IfSequenceNumberLessThanOrEqual.Value()--;
            }
          }

          if (o == Operation::Upload)
          {
            std::vector<uint8_t> pageContent(512);
            auto pageContentStream
                = Azure::Core::IO::MemoryBodyStream(pageContent.data(), pageContent.size());

            Blobs::UploadPagesOptions options;
            options.AccessConditions = accessConditions;
            if (willSuccess)
            {
              EXPECT_NO_THROW(blobClient.UploadPages(0, pageContentStream, options));
            }
            else
            {
              EXPECT_THROW(blobClient.UploadPages(0, pageContentStream, options), StorageException);
            }
          }
          else if (o == Operation::UploadFromUri)
          {
            Blobs::UploadPagesFromUriOptions options;
            options.AccessConditions = accessConditions;
            if (willSuccess)
            {
              EXPECT_NO_THROW(blobClient.UploadPagesFromUri(
                  512, blobClient.GetUrl() + GetSas(), {0, 512}, options));
            }
            else
            {
              EXPECT_THROW(
                  blobClient.UploadPagesFromUri(
                      512, blobClient.GetUrl() + GetSas(), {0, 512}, options),
                  StorageException);
            }
          }
          else if (o == Operation::Clear)
          {
            Blobs::ClearPagesOptions options;
            options.AccessConditions = accessConditions;
            if (willSuccess)
            {
              EXPECT_NO_THROW(blobClient.ClearPages({0, 512}, options));
            }
            else
            {
              EXPECT_THROW(blobClient.ClearPages({0, 512}, options), StorageException);
            }
          }
        }
      }
    }
  }
  TEST_F(PageBlobClientTest, SharedKeySigningHeaderWithSymbols_LIVEONLY_)
  {
    class AdditionalHeaderPolicy final : public Azure::Core::Http::Policies::HttpPolicy {
    public:
      ~AdditionalHeaderPolicy() override {}

      std::unique_ptr<HttpPolicy> Clone() const override
      {
        return std::make_unique<AdditionalHeaderPolicy>(*this);
      }

      std::unique_ptr<Azure::Core::Http::RawResponse> Send(
          Azure::Core::Http::Request& request,
          Azure::Core::Http::Policies::NextHttpPolicy nextPolicy,
          Azure::Core::Context const& context) const override
      {
        // cSpell:disable
        request.SetHeader("x-ms-test", "val");
        request.SetHeader("x-ms-test-", "val");
        request.SetHeader("x-ms-test-a", "val");
        request.SetHeader("x-ms-test-g", "val");
        request.SetHeader("x-ms-test-Z", "val");
        request.SetHeader("x-ms-testa", "val");
        request.SetHeader("x-ms-testd", "val");
        request.SetHeader("x-ms-testx", "val");
        request.SetHeader("x-ms-test--", "val");
        request.SetHeader("x-ms-test-_", "val");
        request.SetHeader("x-ms-test_-", "val");
        request.SetHeader("x-ms-test__", "val");
        request.SetHeader("x-ms-test-a", "val");
        request.SetHeader("x-ms-test-A", "val");
        request.SetHeader("x-ms-test-_A", "val");
        request.SetHeader("x-ms-test_a", "val");
        request.SetHeader("x-ms-test_Z", "val");
        request.SetHeader("x-ms-test_a_", "val");
        request.SetHeader("x-ms-test_a-", "val");
        request.SetHeader("x-ms-test_a-_", "val");
        request.SetHeader("x-ms-testa--", "val");
        request.SetHeader("x-ms-test-a-", "val");
        request.SetHeader("x-ms-test--a", "val");
        request.SetHeader("x-ms-testaa-", "val");
        request.SetHeader("x-ms-testa-a", "val");
        request.SetHeader("x-ms-test-aa", "val");

        request.SetHeader("x-ms-test-!", "val");
        request.SetHeader("x-ms-test-#", "val");
        request.SetHeader("x-ms-test-$", "val");
        request.SetHeader("x-ms-test-%", "val");
        request.SetHeader("x-ms-test-&", "val");
        request.SetHeader("x-ms-test-*", "val");
        request.SetHeader("x-ms-test-+", "val");
        request.SetHeader("x-ms-test-.", "val");
        request.SetHeader("x-ms-test-^", "val");
        request.SetHeader("x-ms-test-_", "val");
        request.SetHeader("x-ms-test-`", "val");
        request.SetHeader("x-ms-test-|", "val");
        request.SetHeader("x-ms-test-~", "val");
        // cSpell:enable
        return nextPolicy.Send(request, context);
      }
    };

    auto clientOptions = InitStorageClientOptions<Blobs::BlobClientOptions>();
    clientOptions.PerOperationPolicies.push_back(std::make_unique<AdditionalHeaderPolicy>());
    auto keyCredential
        = _internal::ParseConnectionString(StandardStorageConnectionString()).KeyCredential;
    auto blockBlobClient
        = Blobs::BlockBlobClient(m_pageBlobClient->GetUrl(), keyCredential, clientOptions);
    EXPECT_NO_THROW(blockBlobClient.GetProperties());
  }

}}} // namespace Azure::Storage::Test
