// HeySticky shim: no DRM. Plain books open; protected ones report an error (07h "Can't open this book").
#include <ContentProtection.h>

namespace freeink {
namespace content {
std::unique_ptr<ContentDecryptor> openProtectedBook(const std::string&, std::string&) { return nullptr; }
}  // namespace content
}  // namespace freeink
