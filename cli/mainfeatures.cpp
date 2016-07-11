#include "./mainfeatures.h"

#include "../application/knownfieldmodel.h"
#include "../misc/utility.h"
#include "../misc/htmlinfo.h"

#include <tagparser/mediafileinfo.h>
#include <tagparser/tag.h>
#include <tagparser/abstracttrack.h>
#include <tagparser/abstractattachment.h>
#include <tagparser/abstractchapter.h>

#include <c++utilities/application/failure.h>
#include <c++utilities/application/commandlineutils.h>
#include <c++utilities/conversion/stringconversion.h>
#include <c++utilities/conversion/conversionexception.h>
#include <c++utilities/io/ansiescapecodes.h>
#include <c++utilities/io/catchiofailure.h>
#include <c++utilities/misc/memory.h>

#include <QDir>

#include <iostream>

using namespace std;
using namespace ApplicationUtilities;
using namespace ConversionUtilities;
using namespace ChronoUtilities;
using namespace EscapeCodes;
using namespace Utility;
using namespace Settings;
using namespace Media;

namespace Cli {

enum class DenotationType
{
    Normal,
    Increment,
    File
};

inline TagType operator| (TagType lhs, TagType rhs)
{
    return static_cast<TagType>(static_cast<unsigned int>(lhs) | static_cast<unsigned int>(rhs));
}

inline TagType &operator|= (TagType &lhs, TagType rhs)
{
    return lhs = static_cast<TagType>(static_cast<unsigned int>(lhs) | static_cast<unsigned int>(rhs));
}

struct FieldDenotation
{
    FieldDenotation(KnownField field);
    KnownField field;
    DenotationType type;
    TagType tagType;
    TagTarget tagTarget;
    std::vector<std::pair<unsigned int, QString> > values;
};

FieldDenotation::FieldDenotation(KnownField field) :
    field(field),
    type(DenotationType::Normal),
    tagType(TagType::Unspecified)
{}

inline bool isDigit(char c)
{
    return c >= '0' && c <= '9';
}

QString incremented(const QString &str, unsigned int toIncrement = 1)
{
    QString res;
    res.reserve(str.size());
    unsigned int value = 0;
    bool hasValue = false;
    for(const QChar &c : str) {
        if(toIncrement && c.isDigit()) {
            value = value * 10 + static_cast<unsigned int>(c.digitValue());
            hasValue = true;
        } else {
            if(hasValue) {
                res.append(QString::number(value + 1));
                hasValue = false;
                --toIncrement;
            }
            res.append(c);
        }
    }
    return res;
}

void printNotifications(NotificationList &notifications, const char *head = nullptr, bool beVerbose = false)
{
    if(!beVerbose) {
        for(const auto &notification : notifications) {
            switch(notification.type()) {
            case NotificationType::Debug:
            case NotificationType::Information:
                break;
            default:
                goto printNotifications;
            }
        }
        return;
    }
    if(!notifications.empty()) {
        printNotifications:
        if(head) {
            cout << head << endl;
        }
        Notification::sortByTime(notifications);
        for(const auto &notification : notifications) {
            switch(notification.type()) {
            case NotificationType::Debug:
                if(beVerbose) {
                    cout << "Debug        ";
                    break;
                } else {
                    continue;
                }
            case NotificationType::Information:
                if(beVerbose) {
                    cout << "Information  ";
                    break;
                } else {
                    continue;
                }
            case NotificationType::Warning:
                cout << "Warning      ";
                break;
            case NotificationType::Critical:
                cout << "Error        ";
                break;
            default:
                ;
            }
            cout << notification.creationTime().toString(DateTimeOutputFormat::TimeOnly) << "   ";
            cout << notification.context() << ": ";
            cout << notification.message() << endl;
        }
    }
}

void printNotifications(const MediaFileInfo &fileInfo, const char *head = nullptr, bool beVerbose = false)
{
    NotificationList notifications;
    fileInfo.gatherRelatedNotifications(notifications);
    printNotifications(notifications, head, beVerbose);
}

const char *const fieldNames = "title album artist genre year comment bpm bps lyricist track disk part totalparts encoder\n"
                               "recorddate performers duration language encodersettings lyrics synchronizedlyrics grouping\n"
                               "recordlabel cover composer rating description";

void printFieldNames(const ArgumentOccurance &occurance)
{
    CMD_UTILS_START_CONSOLE;
    VAR_UNUSED(occurance)
    cout << fieldNames << endl;
}

TagUsage parseUsageDenotation(const Argument &usageArg, TagUsage defaultUsage)
{
    if(usageArg.isPresent()) {
        const auto &val = usageArg.values().front();
        if(!strcmp(val, "never")) {
            return TagUsage::Never;
        } else if(!strcmp(val, "keepexisting")) {
            return TagUsage::KeepExisting;
        } else if(!strcmp(val, "always")) {
            return TagUsage::Always;
        } else {
            cerr << "Warning: The specified tag usage \"" << val << "\" is invalid and will be ignored." << endl;
        }
    }
    return defaultUsage;
}

TagTextEncoding parseEncodingDenotation(const Argument &encodingArg, TagTextEncoding defaultEncoding)
{
    if(encodingArg.isPresent()) {
        const auto &val = encodingArg.values().front();
        if(!strcmp(val, "utf8")) {
            return TagTextEncoding::Utf8;
        } else if(!strcmp(val, "latin1")) {
            return TagTextEncoding::Latin1;
        } else if(!strcmp(val, "utf16be")) {
            return TagTextEncoding::Utf16BigEndian;
        } else if(!strcmp(val, "utf16le")) {
            return TagTextEncoding::Utf16LittleEndian;
        } else if(!strcmp(val, "auto")) {
        } else {
            cerr << "Warning: The specified encoding \"" << val << "\" is invalid and will be ignored." << endl;
        }
    }
    return defaultEncoding;
}

ElementPosition parsePositionDenotation(const Argument &posArg, ElementPosition defaultPos)
{
    if(posArg.isPresent()) {
        const auto &val = posArg.values().front();
        if(!strcmp(val, "front")) {
            return ElementPosition::BeforeData;
        } else if(!strcmp(val, "back")) {
            return ElementPosition::AfterData;
        } else if(!strcmp(val, "keep")) {
            return ElementPosition::Keep;
        } else {
            cerr << "Warning: The specified position \"" << val << "\" is invalid and will be ignored." << endl;
        }
    }
    return defaultPos;
}

uint64 parseUInt64(const Argument &arg, uint64 defaultValue)
{
    if(arg.isPresent()) {
        try {
            if(*arg.values().front() == '0' && *(arg.values().front() + 1) == 'x') {
                return stringToNumber<decltype(parseUInt64(arg, defaultValue))>(arg.values().front() + 2, 16);
            } else {
                return stringToNumber<decltype(parseUInt64(arg, defaultValue))>(arg.values().front());
            }
        } catch(const ConversionException &) {
            cerr << "Warning: The specified value \"" << arg.values().front() << "\" is no valid unsigned integer and will be ignored." << endl;
        }
    }
    return defaultValue;
}

TagTarget::IdContainerType parseIds(const std::string &concatenatedIds)
{
    auto splittedIds = splitString(concatenatedIds, ",", EmptyPartsTreat::Omit);
    TagTarget::IdContainerType convertedIds;
    convertedIds.reserve(splittedIds.size());
    for(const auto &id : splittedIds) {
        try {
            convertedIds.push_back(stringToNumber<TagTarget::IdType>(id));
        } catch(const ConversionException &) {
            cerr << "Warning: The specified ID \"" << id << "\" is invalid and will be ignored." << endl;
        }
    }
    return convertedIds;
}

bool applyTargetConfiguration(TagTarget &target, const std::string &configStr)
{
    if(!configStr.empty()) {
        if(configStr.compare(0, 13, "target-level=") == 0) {
            try {
                target.setLevel(stringToNumber<uint64>(configStr.substr(13)));
            } catch (const ConversionException &) {
                cerr << "Warning: The specified target level \"" << configStr.substr(13) << "\" is invalid and will be ignored." << endl;
            }
        } else if(configStr.compare(0, 17, "target-levelname=") == 0) {
            target.setLevelName(configStr.substr(17));
        } else if(configStr.compare(0, 14, "target-tracks=") == 0) {
            target.tracks() = parseIds(configStr.substr(14));
        } else if(configStr.compare(0, 16, "target-chapters=") == 0) {
            target.chapters() = parseIds(configStr.substr(16));
        } else if(configStr.compare(0, 16, "target-editions=") == 0) {
            target.editions() = parseIds(configStr.substr(16));
        } else if(configStr.compare(0, 17, "target-attachments=") == 0) {
            target.attachments() = parseIds(configStr.substr(17));
        } else if(configStr == "target-reset") {
            target.clear();
        } else {
            return false;
        }
        return true;
    } else {
        return false;
    }
}

vector<FieldDenotation> parseFieldDenotations(const Argument &fieldsArg, bool readOnly)
{
    vector<FieldDenotation> fields;
    if(fieldsArg.isPresent()) {
        const vector<const char *> &fieldDenotations = fieldsArg.values();
        fields.reserve(fieldDenotations.size());
        TagType currentTagType = TagType::Unspecified;
        TagTarget currentTagTarget;
        for(const char *fieldDenotationString : fieldDenotations) {
            // check for tag or target specifier
            const auto fieldDenotationLen = strlen(fieldDenotationString);
            if(!strncmp(fieldDenotationString, "tag:", 4)) {
                if(fieldDenotationLen == 4) {
                    cerr << "Warning: The \"tag\"-specifier has been used with no value(s) and hence is ignored. Possible values are: id3,id3v1,id3v2,itunes,vorbis,matroska,all" << endl;
                } else {
                    TagType tagType = TagType::Unspecified;
                    for(const auto &part : splitString(fieldDenotationString + 4, ",", EmptyPartsTreat::Omit)) {
                        if(part == "id3v1") {
                            tagType |= TagType::Id3v1Tag;
                        } else if(part == "id3v2") {
                            tagType |= TagType::Id3v2Tag;
                        } else if(part == "id3") {
                            tagType |= TagType::Id3v1Tag | TagType::Id3v2Tag;
                        } else if(part == "itunes" || part == "mp4") {
                            tagType |= TagType::Mp4Tag;
                        } else if(part == "vorbis") {
                            tagType |= TagType::VorbisComment;
                        } else if(part == "matroska") {
                            tagType |= TagType::MatroskaTag;
                        } else if(part == "all" || part == "any") {
                            tagType = TagType::Unspecified;
                            break;
                        } else {
                            cerr << "Warning: The value provided with the \"tag\"-specifier is invalid and will be ignored. Possible values are: id3,id3v1,id3v2,itunes,vorbis,matroska,all" << endl;
                            tagType = currentTagType;
                            break;
                        }
                    }
                    currentTagType = tagType;
                    break;
                }
            } else if(applyTargetConfiguration(currentTagTarget, fieldDenotationString)) {
                continue;
            }
            // read field name
            const auto equationPos = strchr(fieldDenotationString, '=');
            size_t fieldNameLen = equationPos ? static_cast<size_t>(equationPos - fieldDenotationString) : strlen(fieldDenotationString);
            // field name might denote increment ("+") or path disclosure (">")
            DenotationType type = DenotationType::Normal;
            if(fieldNameLen && equationPos) {
                switch(*(equationPos - 1)) {
                case '+':
                    type = DenotationType::Increment;
                    --fieldNameLen;
                    break;
                case '>':
                    type = DenotationType::File;
                    --fieldNameLen;
                    break;
                default:
                    ;
                }
            }
            // field name might specify a file index
            unsigned int fileIndex = 0, mult = 1;
            for(const char *digitPos = fieldDenotationString + fieldNameLen - 1; fieldNameLen && isDigit(*digitPos); --fieldNameLen, --digitPos, mult *= 10) {
                fileIndex += static_cast<unsigned int>(*digitPos - '0') * mult;
            }
            if(!fieldNameLen) {
                cerr << "Warning: Ignoring field denotation \"" << fieldDenotationString << "\" because no field name has been specified." << endl;
                continue;
            }
            // parse the denoted filed
            KnownField field;
            if(!strncmp(fieldDenotationString, "title", fieldNameLen)) {
                field = KnownField::Title;
            } else if(!strncmp(fieldDenotationString, "album", fieldNameLen)) {
                field = KnownField::Album;
            } else if(!strncmp(fieldDenotationString, "artist", fieldNameLen)) {
                field = KnownField::Artist;
            } else if(!strncmp(fieldDenotationString, "genre", fieldNameLen)) {
                field = KnownField::Genre;
            } else if(!strncmp(fieldDenotationString, "year", fieldNameLen)) {
                field = KnownField::Year;
            } else if(!strncmp(fieldDenotationString, "comment", fieldNameLen)) {
                field = KnownField::Comment;
            } else if(!strncmp(fieldDenotationString, "bpm", fieldNameLen)) {
                field = KnownField::Bpm;
            } else if(!strncmp(fieldDenotationString, "bps", fieldNameLen)) {
                field = KnownField::Bps;
            } else if(!strncmp(fieldDenotationString, "lyricist", fieldNameLen)) {
                field = KnownField::Lyricist;
            } else if(!strncmp(fieldDenotationString, "track", fieldNameLen)) {
                field = KnownField::TrackPosition;
            } else if(!strncmp(fieldDenotationString, "disk", fieldNameLen)) {
                field = KnownField::DiskPosition;
            } else if(!strncmp(fieldDenotationString, "part", fieldNameLen)) {
                field = KnownField::PartNumber;
            } else if(!strncmp(fieldDenotationString, "totalparts", fieldNameLen)) {
                field = KnownField::TotalParts;
            } else if(!strncmp(fieldDenotationString, "encoder", fieldNameLen)) {
                field = KnownField::Encoder;
            } else if(!strncmp(fieldDenotationString, "recorddate", fieldNameLen)) {
                field = KnownField::RecordDate;
            } else if(!strncmp(fieldDenotationString, "performers", fieldNameLen)) {
                field = KnownField::Performers;
            } else if(!strncmp(fieldDenotationString, "duration", fieldNameLen)) {
                field = KnownField::Length;
            } else if(!strncmp(fieldDenotationString, "language", fieldNameLen)) {
                field = KnownField::Language;
            } else if(!strncmp(fieldDenotationString, "encodersettings", fieldNameLen)) {
                field = KnownField::EncoderSettings;
            } else if(!strncmp(fieldDenotationString, "lyrics", fieldNameLen)) {
                field = KnownField::Lyrics;
            } else if(!strncmp(fieldDenotationString, "synchronizedlyrics", fieldNameLen)) {
                field = KnownField::SynchronizedLyrics;
            } else if(!strncmp(fieldDenotationString, "grouping", fieldNameLen)) {
                field = KnownField::Grouping;
            } else if(!strncmp(fieldDenotationString, "recordlabel", fieldNameLen)) {
                field = KnownField::RecordLabel;
            } else if(!strncmp(fieldDenotationString, "cover", fieldNameLen)) {
                field = KnownField::Cover;
                type = DenotationType::File; // read cover always from file
            } else if(!strncmp(fieldDenotationString, "composer", fieldNameLen)) {
                field = KnownField::Composer;
            } else if(!strncmp(fieldDenotationString, "rating", fieldNameLen)) {
                field = KnownField::Rating;
            } else if(!strncmp(fieldDenotationString, "description", fieldNameLen)) {
                field = KnownField::Description;
            } else {
                // no "KnownField" value matching -> discard the field denotation
                cerr << "Warning: The field name \"" << string(fieldDenotationString, fieldNameLen) << "\" is unknown and will be ingored." << endl;
                continue;
            }
            // add field denotation with parsed values
            fields.emplace_back(field);
            FieldDenotation &fieldDenotation = fields.back();
            fieldDenotation.type = type;
            fieldDenotation.tagType = currentTagType;
            fieldDenotation.tagTarget = currentTagTarget;
            if(equationPos) {
                if(readOnly) {
                    cerr << "Warning: Specified value for \"" << string(fieldDenotationString, fieldNameLen) << "\" will be ignored." << endl;
                } else {
                    fieldDenotation.values.emplace_back(make_pair(mult == 1 ? fieldDenotation.values.size() : fileIndex, QString::fromLocal8Bit(equationPos + 1)));
                }
            }
        }
    }
    return fields;
}

enum class AttachmentAction {
    Add,
    UpdateById,
    UpdateByName,
    RemoveById,
    RemoveByName
};

class AttachmentInfo
{
public:
    AttachmentInfo();
    void apply(AbstractContainer *container);
    void apply(AbstractAttachment *attachment);
    void reset();
    bool next(AbstractContainer *container);

    AttachmentAction action;
    uint64 id;
    string path;
    string name;
    string mime;
    string desc;
};

AttachmentInfo::AttachmentInfo() :
    action(AttachmentAction::Add),
    id(0)
{}

void AttachmentInfo::apply(AbstractContainer *container)
{
    static const string context("applying specified attachments");
    AbstractAttachment *attachment = nullptr;
    bool attachmentFound = false;
    switch(action) {
    case AttachmentAction::Add:
        if(path.empty() || name.empty()) {
            container->addNotification(NotificationType::Critical, "No name or path specified for new attachment to be added.", context);
            return;
        }
        apply(container->createAttachment());
        break;
    case AttachmentAction::UpdateById:
        for(size_t i = 0, count = container->attachmentCount(); i < count; ++i) {
            attachment = container->attachment(i);
            if(attachment->id() == id) {
                apply(attachment);
                attachmentFound = true;
            }
        }
        if(!attachmentFound == true) {
            container->addNotification(NotificationType::Critical, "Attachment with the specified ID \"" + numberToString(id) + "\" does not exist and hence can't be updated.", context);
        }
        break;
    case AttachmentAction::UpdateByName:
        for(size_t i = 0, count = container->attachmentCount(); i < count; ++i) {
            attachment = container->attachment(i);
            if(attachment->name() == name) {
                apply(attachment);
                attachmentFound = true;
            }
        }
        if(!attachmentFound == true) {
            container->addNotification(NotificationType::Critical, "Attachment with the specified name \"" + name + "\" does not exist and hence can't be updated.", context);
        }
        break;
    case AttachmentAction::RemoveById:
        for(size_t i = 0, count = container->attachmentCount(); i < count; ++i) {
            attachment = container->attachment(i);
            if(attachment->id() == id) {
                attachment->setIgnored(true);
                attachmentFound = true;
            }
        }
        if(!attachmentFound == true) {
            container->addNotification(NotificationType::Critical, "Attachment with the specified ID \"" + numberToString(id) + "\" does not exist and hence can't be removed.", context);
        }
        break;
    case AttachmentAction::RemoveByName:
        for(size_t i = 0, count = container->attachmentCount(); i < count; ++i) {
            attachment = container->attachment(i);
            if(attachment->name() == name) {
                attachment->setIgnored(true);
                attachmentFound = true;
            }
        }
        if(!attachmentFound == true) {
            container->addNotification(NotificationType::Critical, "Attachment with the specified name \"" + name + "\" does not exist and hence can't be removed.", context);
        }
        break;
    }
}

void AttachmentInfo::apply(AbstractAttachment *attachment)
{
    if(id) {
        attachment->setId(id);
    }
    if(!path.empty()) {
        attachment->setFile(path);
    }
    if(!name.empty()) {
        attachment->setName(name);
    }
    if(!mime.empty()) {
        attachment->setMimeType(mime);
    }
    if(!desc.empty()) {
        attachment->setDescription(desc);
    }
}

void AttachmentInfo::reset()
{
    action = AttachmentAction::Add;
    id = 0;
    path.clear();
    name.clear();
    mime.clear();
    desc.clear();
}

bool AttachmentInfo::next(AbstractContainer *container)
{
    if(!id && path.empty() && name.empty() && mime.empty() && desc.empty()) {
        // skip empty attachment infos
        return false;
    }
    apply(container);
    reset();
    return true;
}

void generateFileInfo(const ArgumentOccurance &, const Argument &inputFileArg, const Argument &outputFileArg, const Argument &validateArg)
{
    CMD_UTILS_START_CONSOLE;
    try {
        // parse tags
        MediaFileInfo inputFileInfo(inputFileArg.values().front());
        inputFileInfo.setForceFullParse(validateArg.isPresent());
        inputFileInfo.open(true);
        inputFileInfo.parseEverything();
        cout << "Saving file info of \"" << inputFileArg.values().front() << "\" ..." << endl;
        NotificationList origNotify;
        QFile file(QString::fromLocal8Bit(outputFileArg.values().front()));
        if(file.open(QFile::WriteOnly) && file.write(HtmlInfo::generateInfo(inputFileInfo, origNotify)) && file.flush()) {
            cout << "File information has been saved to \"" << outputFileArg.values().front() << "\"." << endl;
        } else {
            cerr << "Error: An IO error occured when writing the file \"" << outputFileArg.values().front() << "\"." << endl;
        }
    } catch(const ApplicationUtilities::Failure &) {
        cerr << "Error: A parsing failure occured when reading the file \"" << inputFileArg.values().front() << "\"." << endl;
    } catch(...) {
        ::IoUtilities::catchIoFailure();
        cerr << "Error: An IO failure occured when reading the file \"" << inputFileArg.values().front() << "\"." << endl;
    }
}

void printProperty(const char *propName, const char *value, const char *suffix = nullptr, size_t intention = 4)
{
    if(*value) {
        for(; intention; --intention) {
            cout << ' ';
        }
        cout << propName;
        for(intention = strlen(propName); intention < 30; ++intention) {
            cout << ' ';
        }
        cout << value;
        if(suffix) {
            cout << ' ' << suffix;
        }
        cout << endl;
    }
}

void printProperty(const char *propName, const string &value, const char *suffix = nullptr, size_t intention = 4)
{
    printProperty(propName, value.data(), suffix, intention);
}

void printProperty(const char *propName, TimeSpan timeSpan, const char *suffix = nullptr, size_t intention = 4)
{
    if(!timeSpan.isNull()) {
        printProperty(propName, timeSpan.toString(TimeSpanOutputFormat::WithMeasures), suffix, intention);
    }
}

void printProperty(const char *propName, DateTime dateTime, const char *suffix = nullptr, size_t intention = 4)
{
    if(!dateTime.isNull()) {
        printProperty(propName, dateTime.toString(), suffix, intention);
    }
}

template<typename intType>
void printProperty(const char *propName, const intType value, const char *suffix = nullptr, bool force = false, size_t intention = 4)
{
    if(value != 0 || force) {
        printProperty(propName, numberToString<intType>(value), suffix, intention);
    }
}

void displayFileInfo(const ArgumentOccurance &, const Argument &filesArg, const Argument &verboseArg)
{
    CMD_UTILS_START_CONSOLE;
    if(!filesArg.isPresent() || filesArg.values().empty()) {
        cout << "Error: No files have been specified." << endl;
        return;
    }
    MediaFileInfo fileInfo;
    for(const auto &file : filesArg.values()) {
        try {
            // parse tags
            fileInfo.setPath(file);
            fileInfo.open(true);
            fileInfo.parseTracks();
            fileInfo.parseAttachments();
            fileInfo.parseChapters();
            cout << "Technical information for \"" << file << "\":" << endl;
            cout << "  Container format: " << fileInfo.containerFormatName() << endl;
            {
                if(const auto container = fileInfo.container()) {
                    size_t segmentIndex = 0;
                    for(const auto &title : container->titles()) {
                        if(segmentIndex) {
                            printProperty("Title", title + " (segment " + numberToString(++segmentIndex) + ")");
                        } else {
                            ++segmentIndex;
                            printProperty("Title", title);
                        }
                    }
                    printProperty("Document type", container->documentType());
                    printProperty("Read version", container->readVersion());
                    printProperty("Version", container->version());
                    printProperty("Document read version", container->doctypeReadVersion());
                    printProperty("Document version", container->doctypeVersion());
                    printProperty("Duration", container->duration());
                    printProperty("Creation time", container->creationTime());
                    printProperty("Modification time", container->modificationTime());
                }
                if(fileInfo.paddingSize()) {
                    printProperty("Padding", dataSizeToString(fileInfo.paddingSize()));
                }
            }
            { // tracks
                const auto tracks = fileInfo.tracks();
                if(!tracks.empty()) {
                    cout << "  Tracks:" << endl;
                    for(const auto *track : tracks) {
                        printProperty("ID", track->id(), nullptr, true);
                        printProperty("Name", track->name());
                        printProperty("Type", track->mediaTypeName());
                        const char *fmtName = track->formatName(), *fmtAbbr = track->formatAbbreviation();
                        printProperty("Format", fmtName);
                        if(strcmp(fmtName, fmtAbbr)) {
                            printProperty("Abbreviation", fmtAbbr);
                        }
                        printProperty("Extensions", track->format().extensionName());
                        printProperty("Raw format ID", track->formatId());
                        if(track->size()) {
                            printProperty("Size", dataSizeToString(track->size(), true));
                        }
                        printProperty("Duration", track->duration());
                        printProperty("FPS", track->fps());
                        if(track->channelConfigString()) {
                            printProperty("Channel config", track->channelConfigString());
                        } else {
                            printProperty("Channel count", track->channelCount());
                        }
                        if(track->extensionChannelConfigString()) {
                            printProperty("Extension channel config", track->extensionChannelConfigString());
                        }
                        printProperty("Bitrate", track->bitrate(), "kbit/s");
                        printProperty("Bits per sample", track->bitsPerSample());
                        printProperty("Sampling frequency", track->samplingFrequency(), "Hz");
                        printProperty("Extension sampling frequency", track->extensionSamplingFrequency(), "Hz");
                        printProperty("Sample count", track->sampleCount());
                        printProperty("Creation time", track->creationTime());
                        printProperty("Modification time", track->modificationTime());
                        cout << endl;
                    }
                } else {
                    cout << " File has no (supported) tracks." << endl;
                }
            }
            { // attachments
                const auto attachments = fileInfo.attachments();
                if(!attachments.empty()) {
                    for(const auto *attachment : attachments) {
                        printProperty("ID", attachment->id());
                        printProperty("Name", attachment->name());
                        printProperty("MIME-type", attachment->mimeType());
                        printProperty("Label", attachment->label());
                        printProperty("Description", attachment->description());
                        if(attachment->data()) {
                            printProperty("Size", dataSizeToString(attachment->data()->size(), true));
                        }
                        cout << endl;
                    }
                }
            }
            { // chapters
                const auto chapters = fileInfo.chapters();
                if(!chapters.empty()) {
                    for(const auto *chapter : chapters) {
                        printProperty("ID", chapter->id());
                        if(!chapter->names().empty()) {
                            printProperty("Name", static_cast<string>(chapter->names().front()));
                        }
                        if(!chapter->startTime().isNegative()) {
                            printProperty("Start time", chapter->startTime().toString());
                        }
                        if(!chapter->endTime().isNegative()) {
                            printProperty("End time", chapter->endTime().toString());
                        }
                        cout << endl;
                    }
                }
            }
        } catch(const ApplicationUtilities::Failure &) {
            cerr << "Error: A parsing failure occured when reading the file \"" << file << "\"." << endl;
        } catch(...) {
            ::IoUtilities::catchIoFailure();
            cerr << "Error: An IO failure occured when reading the file \"" << file << "\"." << endl;
        }
        printNotifications(fileInfo, "Parsing notifications:", verboseArg.isPresent());
        cout << endl;
    }
}

void displayTagInfo(const Argument &fieldsArg, const Argument &filesArg, const Argument &verboseArg)
{
    CMD_UTILS_START_CONSOLE;
    if(!filesArg.isPresent() || filesArg.values().empty()) {
        cout << "Error: No files have been specified." << endl;
        return;
    }
    const auto fields = parseFieldDenotations(fieldsArg, true);
    MediaFileInfo fileInfo;
    for(const auto &file : filesArg.values()) {
        try {
            // parse tags
            fileInfo.setPath(file);
            fileInfo.open(true);
            fileInfo.parseTags();
            cout << "Tag information for \"" << file << "\":" << endl;
            const auto tags = fileInfo.tags();
            if(tags.size()) {
                // iterate through all tags
                for(const auto *tag : tags) {
                    // determine tag type
                    TagType tagType = tag->type();
                    // write tag name and target, eg. MP4/iTunes tag
                    cout << tag->typeName();
                    if(!tag->target().isEmpty()) {
                        cout << " targeting \"" << tag->targetString() << "\"";
                    }
                    cout << endl;
                    // iterate through fields specified by the user
                    if(fields.empty()) {
                        for(auto field = firstKnownField; field != KnownField::Invalid; field = nextKnownField(field)) {
                            const auto &value = tag->value(field);
                            if(!value.isEmpty()) {
                                // write field name
                                const char *fieldName = KnownFieldModel::fieldName(field);
                                cout << ' ' << fieldName;
                                // write padding
                                for(auto i = strlen(fieldName); i < 18; ++i) {
                                    cout << ' ';
                                }
                                // write value
                                try {
                                    const auto textValue = tagValueToQString(value);
                                    if(textValue.isEmpty()) {
                                        cout << "can't display here (see --extract)";
                                    } else {
                                        cout << textValue.toLocal8Bit().data();
                                    }
                                } catch(const ConversionException &) {
                                    cout << "conversion error";
                                }
                                cout << endl;
                            }
                        }
                    } else {
                        for(const FieldDenotation &fieldDenotation : fields) {
                            const auto &value = tag->value(fieldDenotation.field);
                            if(fieldDenotation.tagType == TagType::Unspecified || (fieldDenotation.tagType | tagType) != TagType::Unspecified) {
                                // write field name
                                const char *fieldName = KnownFieldModel::fieldName(fieldDenotation.field);
                                cout << ' ' << fieldName;
                                // write padding
                                for(auto i = strlen(fieldName); i < 18; ++i) {
                                    cout << ' ';
                                }
                                // write value
                                if(value.isEmpty()) {
                                    cout << "none";
                                } else {
                                    try {
                                        const auto textValue = tagValueToQString(value);
                                        if(textValue.isEmpty()) {
                                            cout << "can't display here (see --extract)";
                                        } else {
                                            cout << textValue.toLocal8Bit().data();
                                        }
                                    } catch(const ConversionException &) {
                                        cout << "conversion error";
                                    }
                                }
                                cout << endl;
                            }
                        }
                    }
                }
            } else {
                cout << " File has no (supported) tag information." << endl;
            }
        } catch(const ApplicationUtilities::Failure &) {
            cerr << "Error: A parsing failure occured when reading the file \"" << file << "\"." << endl;
        } catch(...) {
            ::IoUtilities::catchIoFailure();
            cerr << "Error: An IO failure occured when reading the file \"" << file << "\"." << endl;
        }
        printNotifications(fileInfo, "Parsing notifications:", verboseArg.isPresent());
        cout << endl;
    }
}

void setTagInfo(const SetTagInfoArgs &args)
{
    CMD_UTILS_START_CONSOLE;
    if(!args.filesArg.isPresent() || args.filesArg.values().empty()) {
        cerr << "Error: No files have been specified." << endl;
        return;
    }
    auto fields = parseFieldDenotations(args.valuesArg, false);
    if(fields.empty() && args.attachmentsArg.values().empty() && args.docTitleArg.values().empty()) {
        cerr << "Error: No fields/attachments have been specified." << endl;
        return;
    }
    // determine required targets
    vector<TagTarget> requiredTargets;
    for(const FieldDenotation &fieldDenotation : fields) {
        if(find(requiredTargets.cbegin(), requiredTargets.cend(), fieldDenotation.tagTarget) == requiredTargets.cend()) {
            requiredTargets.push_back(fieldDenotation.tagTarget);
        }
    }
    // determine targets to remove
    vector<TagTarget> targetsToRemove;
    targetsToRemove.emplace_back();
    bool validRemoveTargetsSpecified = false;
    if(args.removeTargetsArg.isPresent()) {
        for(const auto &targetDenotation : args.removeTargetsArg.values()) {
            if(!strcmp(targetDenotation, ",")) {
                if(validRemoveTargetsSpecified) {
                    targetsToRemove.emplace_back();
                }
            } else if(applyTargetConfiguration(targetsToRemove.back(), targetDenotation)) {
                validRemoveTargetsSpecified = true;
            } else {
                cerr << "Warning: The given target specification \"" << targetDenotation << "\" is invalid and will be ignored." << endl;
            }
        }
    }
    // parse other settings
    uint32 id3v2Version = 3;
    if(args.id3v2VersionArg.isPresent()) {
        try {
            id3v2Version = stringToNumber<uint32>(args.id3v2VersionArg.values().front());
            if(id3v2Version < 1 || id3v2Version > 4) {
                throw ConversionException();
            }
        } catch (const ConversionException &) {
            id3v2Version = 3;
            cerr << "Warning: The specified ID3v2 version \"" << args.id3v2VersionArg.values().front() << "\" is invalid and will be ingored." << endl;
        }
    }
    const TagTextEncoding denotedEncoding = parseEncodingDenotation(args.encodingArg, TagTextEncoding::Utf8);
    const TagUsage id3v1Usage = parseUsageDenotation(args.id3v1UsageArg, TagUsage::KeepExisting);
    const TagUsage id3v2Usage = parseUsageDenotation(args.id3v2UsageArg, TagUsage::Always);
    MediaFileInfo fileInfo;
    fileInfo.setMinPadding(parseUInt64(args.minPaddingArg, 0));
    fileInfo.setMaxPadding(parseUInt64(args.maxPaddingArg, 0));
    fileInfo.setPreferredPadding(parseUInt64(args.prefPaddingArg, 0));
    fileInfo.setTagPosition(parsePositionDenotation(args.tagPosArg, ElementPosition::BeforeData));
    fileInfo.setForceTagPosition(args.forceTagPosArg.isPresent());
    fileInfo.setIndexPosition(parsePositionDenotation(args.indexPosArg, ElementPosition::BeforeData));
    fileInfo.setForceIndexPosition(args.forceIndexPosArg.isPresent());
    fileInfo.setForceRewrite(args.forceRewriteArg.isPresent());
    // iterate through all specified files
    unsigned int fileIndex = 0;
    static const string context("setting tags");
    NotificationList notifications;
    for(const auto &file : args.filesArg.values()) {
        try {
            // parse tags
            cout << "Setting tag information for \"" << file << "\" ..." << endl;
            notifications.clear();
            fileInfo.setPath(file);
            fileInfo.parseTags();
            fileInfo.parseTracks();
            vector<Tag *> tags;
            // remove tags with the specified targets
            if(validRemoveTargetsSpecified) {
                fileInfo.tags(tags);
                for(auto *tag : tags) {
                    if(find(targetsToRemove.cbegin(), targetsToRemove.cend(), tag->target()) != targetsToRemove.cend()) {
                        fileInfo.removeTag(tag);
                    }
                }
                tags.clear();
            }
            // create new tags according to settings
            fileInfo.createAppropriateTags(args.treatUnknownFilesAsMp3FilesArg.isPresent(), id3v1Usage, id3v2Usage, args.mergeMultipleSuccessiveTagsArg.isPresent(), !args.id3v2VersionArg.isPresent(), id3v2Version, requiredTargets);
            auto container = fileInfo.container();
            bool docTitleModified = false;
            if(args.docTitleArg.isPresent() && !args.docTitleArg.values().empty()) {
                if(container && container->supportsTitle()) {
                    size_t segmentIndex = 0, segmentCount = container->titles().size();
                    for(const auto &newTitle : args.docTitleArg.values()) {
                        if(segmentIndex < segmentCount) {
                            container->setTitle(newTitle, segmentIndex);
                            docTitleModified = true;
                        } else {
                            cerr << "Warning: The specified document title \"" << newTitle << "\" can not be set because the file has not that many segments." << endl;
                        }
                        ++segmentIndex;
                    }
                } else {
                    cerr << "Warning: Setting the document title is not supported for the file." << endl;
                }
            }
            fileInfo.tags(tags);
            if(!tags.empty()) {
                // iterate through all tags
                for(auto *tag : tags) {
                    if(args.removeOtherFieldsArg.isPresent()) {
                        tag->removeAllFields();
                    }
                    auto tagType = tag->type();
                    bool targetSupported = tag->supportsTarget();
                    auto tagTarget = tag->target();
                    for(FieldDenotation &fieldDenotation : fields) {
                        if((fieldDenotation.tagType == TagType::Unspecified
                            || (fieldDenotation.tagType | tagType) != TagType::Unspecified)
                                && (!targetSupported || fieldDenotation.tagTarget == tagTarget)) {
                            pair<unsigned int, QString> *selectedDenotatedValue = nullptr;
                            for(auto &someDenotatedValue : fieldDenotation.values) {
                                if(someDenotatedValue.first <= fileIndex) {
                                    if(!selectedDenotatedValue || (someDenotatedValue.first > selectedDenotatedValue->first)) {
                                        selectedDenotatedValue = &someDenotatedValue;
                                    }
                                }
                            }
                            if(selectedDenotatedValue) {
                                if(fieldDenotation.type == DenotationType::File) {
                                    if(selectedDenotatedValue->second.isEmpty()) {
                                        tag->setValue(fieldDenotation.field, TagValue());
                                    } else {
                                        try {
                                            MediaFileInfo fileInfo(selectedDenotatedValue->second.toLocal8Bit().constData());
                                            fileInfo.open(true);
                                            fileInfo.parseContainerFormat();
                                            auto buff = make_unique<char []>(fileInfo.size());
                                            fileInfo.stream().seekg(0);
                                            fileInfo.stream().read(buff.get(), fileInfo.size());
                                            TagValue value(move(buff), fileInfo.size(), TagDataType::Picture);
                                            value.setMimeType(fileInfo.mimeType());
                                            tag->setValue(fieldDenotation.field, move(value));
                                        } catch(const Media::Failure &) {
                                            fileInfo.addNotification(NotificationType::Critical, "Unable to parse specified cover file.", context);
                                        } catch(...) {
                                            ::IoUtilities::catchIoFailure();
                                            fileInfo.addNotification(NotificationType::Critical, "An IO error occured when parsing the specified cover file.", context);
                                        }
                                    }
                                } else {
                                    TagTextEncoding usedEncoding = denotedEncoding;
                                    if(!tag->canEncodingBeUsed(denotedEncoding)) {
                                        usedEncoding = tag->proposedTextEncoding();
                                    }
                                    tag->setValue(fieldDenotation.field, qstringToTagValue(selectedDenotatedValue->second, usedEncoding));
                                    if(fieldDenotation.type == DenotationType::Increment && tag == tags.back()) {
                                        selectedDenotatedValue->second = incremented(selectedDenotatedValue->second);
                                    }
                                }
                            }
                        }
                    }
                }
            } else {
                fileInfo.addNotification(NotificationType::Critical, "Can not create appropriate tags for file.", context);
            }
            bool attachmentsModified = false;
            if(args.attachmentsArg.isPresent() || args.removeExistingAttachmentsArg.isPresent()) {
                static const string context("setting attachments");
                fileInfo.parseAttachments();
                if(fileInfo.attachmentsParsingStatus() == ParsingStatus::Ok) {
                    if(container) {
                        // ignore all existing attachments if argument is specified
                        if(args.removeExistingAttachmentsArg.isPresent()) {
                            for(size_t i = 0, count = container->attachmentCount(); i < count; ++i) {
                                container->attachment(i)->setIgnored(false);
                            }
                            attachmentsModified = true;
                        }
                        // add/update/remove attachments explicitely
                        AttachmentInfo currentInfo;
                        for(const char *value : args.attachmentsArg.values()) {
                            if(!strcmp(value, ",")) {
                                attachmentsModified |= currentInfo.next(container);
                            } else if(!strcmp(value, "add")) {
                                currentInfo.action = AttachmentAction::Add;
                            } else if(!strcmp(value, "update-by-id")) {
                                currentInfo.action = AttachmentAction::UpdateById;
                            } else if(!strcmp(value, "update-by-name")) {
                                currentInfo.action = AttachmentAction::UpdateByName;
                            } else if(!strcmp(value, "remove-by-id")) {
                                currentInfo.action = AttachmentAction::RemoveById;
                            } else if(!strcmp(value, "remove-by-name")) {
                                currentInfo.action = AttachmentAction::RemoveByName;
                            } else if(!strncmp(value, "id=", 3)) {
                                try {
                                    currentInfo.id = stringToNumber<uint64, string>(value + 3);
                                } catch(const ConversionException &) {
                                    container->addNotification(NotificationType::Warning, "The specified attachment ID \"" + string(value + 3) + "\" is invalid.", context);
                                }
                            } else if(!strncmp(value, "path=", 5)) {
                                currentInfo.path = value + 5;
                            } else if(!strncmp(value, "name=", 5)) {
                                currentInfo.name = value + 5;
                            } else if(!strncmp(value, "mime=", 5)) {
                                currentInfo.mime = value + 5;
                            } else if(!strncmp(value, "desc=", 5)) {
                                currentInfo.desc = value + 5;
                            } else {
                                container->addNotification(NotificationType::Warning, "The attachment specification \"" + string(value) + "\" is invalid and will be ignored.", context);
                            }
                        }
                        attachmentsModified |= currentInfo.next(container);
                    } else {
                        fileInfo.addNotification(NotificationType::Critical, "Unable to assign attachments because the container object has not been initialized.", context);
                    }
                } else {
                    // notification will be added by the file info automatically
                }
            }
            if(!tags.empty() || docTitleModified || attachmentsModified) {
                try {
                    // save parsing notifications because notifications of sub objects like tags, tracks, ... will be gone after applying changes
                    fileInfo.gatherRelatedNotifications(notifications);
                    fileInfo.invalidateNotifications();
                    fileInfo.applyChanges();
                    fileInfo.gatherRelatedNotifications(notifications);
                    cout << "Changes have been applied." << endl;
                } catch(const ApplicationUtilities::Failure &) {
                    cerr << "Error: Failed to apply changes." << endl;
                }
            } else {
                cerr << "Warning: No changed to be applied." << endl;
            }
        } catch(const ApplicationUtilities::Failure &) {
            cerr << "Error: A parsing failure occured when reading/writing the file \"" << file << "\"." << endl;
        } catch(...) {
            ::IoUtilities::catchIoFailure();
            cerr << "Error: An IO failure occured when reading/writing the file \"" << file << "\"." << endl;
        }
        printNotifications(notifications, "Notifications:", args.verboseArg.isPresent());
        ++fileIndex;
    }
}

void extractField(const Argument &fieldsArg, const Argument &inputFileArg, const Argument &outputFileArg, const Argument &verboseArg)
{
    CMD_UTILS_START_CONSOLE;
    const auto fields = parseFieldDenotations(fieldsArg, true);
    if(fields.size() != 1) {
        cerr << "Error: Excactly one field needs to be specified." << endl;
        return;
    }
    MediaFileInfo inputFileInfo;
    try {
        // parse tags
        inputFileInfo.setPath(inputFileArg.values().front());
        inputFileInfo.open(true);
        inputFileInfo.parseTags();
        cout << "Extracting " << fieldsArg.values().front() << " of \"" << inputFileArg.values().front() << "\" ..." << endl;
        auto tags = inputFileInfo.tags();
        vector<pair<const TagValue *, string> > values;
        // iterate through all tags
        for(const Tag *tag : tags) {
            for(const auto &fieldDenotation : fields) {
                const auto &value = tag->value(fieldDenotation.field);
                if(!value.isEmpty()) {
                    values.emplace_back(&value, joinStrings({tag->typeName(), numberToString(values.size())}, "-"));
                }
            }
        }
        if(values.empty()) {
            cerr << "File has no (supported) " << fieldsArg.values().front() << " field." << endl;
        } else {
            string outputFilePathWithoutExtension, outputFileExtension;
            if(values.size() > 1) {
                outputFilePathWithoutExtension = BasicFileInfo::pathWithoutExtension(outputFileArg.values().front());
                outputFileExtension = BasicFileInfo::extension(outputFileArg.values().front());
            }
            for(const auto &value : values) {
                fstream outputFileStream;
                outputFileStream.exceptions(ios_base::failbit | ios_base::badbit);
                auto path = values.size() > 1 ? joinStrings({outputFilePathWithoutExtension, "-", value.second, outputFileExtension}) : outputFileArg.values().front();
                try {
                    outputFileStream.open(path, ios_base::out | ios_base::binary);
                    outputFileStream.write(value.first->dataPointer(), value.first->dataSize());
                    outputFileStream.flush();
                    cout << "Value has been saved to \"" << path << "\"." << endl;
                } catch(...) {
                    ::IoUtilities::catchIoFailure();
                    cerr << "Error: An IO error occured when writing the file \"" << path << "\"." << endl;
                }
            }
        }
    } catch(const ApplicationUtilities::Failure &) {
        cerr << "Error: A parsing failure occured when reading the file \"" << inputFileArg.values().front() << "\"." << endl;
    } catch(...) {
        ::IoUtilities::catchIoFailure();
        cerr << "Error: An IO failure occured when reading the file \"" << inputFileArg.values().front() << "\"." << endl;
    }
    printNotifications(inputFileInfo, "Parsing notifications:", verboseArg.isPresent());
}

}
