/*
 * Copyright (C) Canonical, Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "snapshot_settings_handler.h"
#include "multipass/exceptions/snapshot_exceptions.h"

#include <multipass/constants.h>
#include <multipass/format.h>

#include <QString>

namespace mp = multipass;

namespace
{
constexpr auto name_suffix = "name";
constexpr auto comment_suffix = "comment";
constexpr auto common_exception_msg = "Cannot access snapshot settings";

QRegularExpression make_key_regex()
{
    const auto instance_pattern = QStringLiteral("(?<instance>.+)");
    const auto snapshot_pattern = QStringLiteral("(?<snapshot>.+)");

    const auto property_template = QStringLiteral("(?<property>%1)");
    const auto either_prop = QStringList{name_suffix, comment_suffix}.join("|");
    const auto property_pattern = property_template.arg(either_prop);

    const auto key_template = QStringLiteral(R"(%1\.%2\.%3\.%4)");
    const auto key_pattern =
        key_template.arg(mp::daemon_settings_root, instance_pattern, snapshot_pattern, property_pattern);

    return QRegularExpression{QRegularExpression::anchoredPattern(key_pattern)};
}

std::tuple<std::string, QString, std::string> parse_key(const QString& key)
{
    static const auto key_regex = make_key_regex();

    auto match = key_regex.match(key);
    if (match.hasMatch())
    {
        auto instance = match.captured("instance");
        auto snapshot = match.captured("snapshot");
        auto property = match.captured("property");

        assert(!instance.isEmpty() && !snapshot.isEmpty() && !property.isEmpty());
        return {instance.toStdString(), snapshot, property.toStdString()};
    }

    throw mp::UnrecognizedSettingException{key};
}
} // namespace

mp::SnapshotSettingsException::SnapshotSettingsException(const std::string& missing_instance, const std::string& detail)
    : SettingsException{fmt::format("{}; instance: {}; reason: {}", common_exception_msg, missing_instance, detail)}
{
}

mp::SnapshotSettingsException::SnapshotSettingsException(const std::string& detail)
    : SettingsException{fmt::format("{}; reason: {}", common_exception_msg, detail)}
{
}

mp::SnapshotSettingsHandler::SnapshotSettingsHandler(
    std::unordered_map<std::string, VirtualMachine::ShPtr>& operative_instances,
    const std::unordered_map<std::string, VirtualMachine::ShPtr>& deleted_instances,
    const std::unordered_set<std::string>& preparing_instances)
    : operative_instances{operative_instances},
      deleted_instances{deleted_instances},
      preparing_instances{preparing_instances}
{
}

std::set<QString> mp::SnapshotSettingsHandler::keys() const
{
    static const auto key_template = QStringLiteral("%1.%2.%3.%4").arg(daemon_settings_root);
    std::set<QString> ret;

    for (const auto* instance_map : {&const_operative_instances, &deleted_instances})
        for (const auto& [vm_name, vm] : *instance_map)
            for (const auto& snapshot : vm->view_snapshots())
                for (const auto& suffix : {name_suffix, comment_suffix})
                    ret.insert(key_template.arg(vm_name.c_str(), snapshot->get_name().c_str(), suffix));

    return ret;
}

QString mp::SnapshotSettingsHandler::get(const QString& key) const
{
    auto [instance_name, snapshot_name, property] = parse_key(key);

    auto snapshot = find_snapshot(instance_name, snapshot_name.toStdString());

    if (property == name_suffix)
        return snapshot_name; // not very useful, but for completeness

    assert(property == comment_suffix);
    return QString::fromStdString(snapshot->get_comment());
}

void mp::SnapshotSettingsHandler::set(const QString& key, const QString& val)
{
    auto [instance_name, snapshot_name, property] = parse_key(key);
    auto snapshot_name_stdstr = snapshot_name.toStdString();
    auto val_stdstr = val.toStdString();

    if (property == name_suffix)
    {
        // TODO@no-merge need to verify name validity/uniqueness and update map
        modify_vm(instance_name)->rename_snapshot(snapshot_name_stdstr, val_stdstr);
    }
    else
    {
        assert(property == comment_suffix);
        auto [vm, snapshot] = modify_snapshot(instance_name, snapshot_name_stdstr);
        snapshot->set_comment(val_stdstr);
    }
    // TODO@no-merge persist (ideally would happen automatically in setters)
}

auto mp::SnapshotSettingsHandler::find_snapshot(const std::string& instance_name,
                                                const std::string& snapshot_name) const
    -> std::shared_ptr<const Snapshot>
{
    try
    {
        return find_instance(instance_name)->get_snapshot(snapshot_name);
    }
    catch (const NoSuchSnapshot& e)
    {
        throw SnapshotSettingsException{e.what()};
    }
}

auto mp::SnapshotSettingsHandler::find_instance(const std::string& instance_name) const
    -> std::shared_ptr<const VirtualMachine>
{
    if (preparing_instances.find(instance_name) != preparing_instances.end())
        throw SnapshotSettingsException{instance_name, "instance is being prepared"};

    for (const auto* instance_map : {&const_operative_instances, &deleted_instances})
    {
        try
        {
            return instance_map->at(instance_name);
        }
        catch (std::out_of_range&)
        {
            continue; // we're OK reading snapshot properties of deleted instances
        }
    }

    throw SnapshotSettingsException{instance_name, "no such instance"};
}

auto mp::SnapshotSettingsHandler::modify_vm(const std::string& instance_name) -> std::shared_ptr<VirtualMachine>
{
    return nullptr; // TODO@no-merge
}

auto mp::SnapshotSettingsHandler::modify_snapshot(const std::string& instance_name, const std::string& snapshot_name)
    -> std::pair<std::shared_ptr<VirtualMachine>, std::shared_ptr<Snapshot>>
{
    return {nullptr, nullptr}; // TODO@no-merge
}
